#!/usr/bin/env bash
# Test the loaded kernel driver without programming the sensor over I2C.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if command -v v4l2-ctl >/dev/null 2>&1; then
    ctl="v4l2-ctl"
elif [[ -x "$REPO_ROOT/tools/usr/bin/v4l2-ctl" ]]; then
    ctl="$REPO_ROOT/tools/usr/bin/v4l2-ctl"
else
    echo "v4l2-ctl not found in PATH or $REPO_ROOT/tools/usr/bin/" >&2
    exit 1
fi

stamp="$(date +%Y%m%d-%H%M%S)"
output_dir="$REPO_ROOT/build/test_results"
mkdir -p "$output_dir"
log="$output_dir/native120-$stamp.log"
registers="$output_dir/native120-$stamp-registers.txt"

"$ctl" --set-ctrl=sensor_mode=2,bypass_mode=0,preferred_stride=2688
# STREAMON updates the control ranges for the selected mode. Before it,
# frame_rate is still capped by the previous mode (30 or 60fps).
timeout -k 3 10 "$ctl" --set-fmt-video=width=1332,height=990,pixelformat=RG10 \
    --set-ctrl=sensor_mode=2,bypass_mode=0,override_enable=0 \
    --stream-mmap --stream-count=1 --stream-to=/dev/null
timeout -k 3 15 "$ctl" --set-fmt-video=width=1332,height=990,pixelformat=RG10 \
    --set-ctrl=sensor_mode=2,bypass_mode=0,override_enable=1,frame_rate=120000000,exposure=4000,gain=32 \
    --stream-mmap --stream-count=600 --stream-to=/dev/null --verbose >"$log" 2>&1 &
capture_pid=$!
sleep 1
python3 "$REPO_ROOT/tools/read_sensor.py" >"$registers" || true
capture_status=0
wait "$capture_pid" || capture_status=$?
python3 - "$log" "$registers" "$capture_status" <<'PY'
import json,re,sys
from pathlib import Path
log=Path(sys.argv[1]).read_text()
entries=[(int(seq),float(ts),flags) for seq,ts,flags in
         re.findall(r'cap dqbuf:.*?seq:\s*(\d+).*?ts:\s*([\d.]+).*?field: None \(([^)]*)\)',log)]
errors=sum('error' in flags for _,_,flags in entries)
sequences=[seq for seq,_,_ in entries]
missing=sum(b-a-1 for a,b in zip(sequences,sequences[1:]))
fps=(len(entries)-1)/(entries[-1][1]-entries[0][1]) if len(entries)>1 else 0
registers=Path(sys.argv[2]).read_text()
geometry='034c: 0534' in registers and '034e: 03de' in registers
result=dict(frames=len(entries),errors=errors,missing_sequences=missing,
            measured_fps=fps,sensor_geometry_matches=geometry,
            capture_exit_status=int(sys.argv[3]),capture_path='native kernel driver; no I2C writes')
passed=(len(entries)==600 and errors==0 and missing==0 and 119<fps<121
        and geometry and int(sys.argv[3])==0)
result['passed']=passed
Path(sys.argv[1]+'.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
sys.exit(0 if passed else 1)
PY
