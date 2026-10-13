#!/usr/bin/env bash
# Verify IMX477 120fps ISP delivery using Direct Libargus EGLStream pipeline.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

probe_bin="$REPO_ROOT/build/bin/argus_fps_probe"

if [[ ! -x "$probe_bin" || "$SCRIPT_DIR/argus_fps_probe.cpp" -nt "$probe_bin" ]]; then
    echo "argus_fps_probe not found, compiling..."
    mkdir -p "$REPO_ROOT/build/bin"
    g++ -O3 "$SCRIPT_DIR/argus_fps_probe.cpp" -o "$probe_bin" \
        -I/usr/src/jetson_multimedia_api/include \
        -L/usr/lib/aarch64-linux-gnu/nvidia -lnvargus -lEGL
fi

mode="${1:-2}"
frames="${2:-7261}"
[[ "$mode" =~ ^[012]$ && "$frames" =~ ^[0-9]+$ ]] || { echo 'Usage: verify_isp.sh [mode 0|1|2] [frames]' >&2; exit 2; }
[[ ${#frames} -le 6 && "$frames" -ge 2 && "$frames" -le 100000 ]] || { echo 'frames must be 2..100000' >&2; exit 2; }
stamp="$(date +%Y%m%d-%H%M%S)"
output_dir="$REPO_ROOT/build/test_results"
mkdir -p "$output_dir"
json_out="$output_dir/isp-mode$mode-$stamp.json"
fps=120
[[ "$mode" == 0 ]] && fps=30
[[ "$mode" == 1 ]] && fps=60
deadline=$((frames / fps + 30))
echo "Running ISP mode $mode verification ($frames frames)..."
timeout -k 5 "$deadline" "$probe_bin" --mode "$mode" --frames "$frames" --output "$json_out" --exposure-us 4000 --gain 2.0

echo "Verification complete. Results saved to: $json_out"
cat "$json_out"
python3 "$SCRIPT_DIR/audit_isp_results.py" "$json_out"
