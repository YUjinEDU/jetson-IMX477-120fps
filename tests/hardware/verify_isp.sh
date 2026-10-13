#!/usr/bin/env bash
# Verify IMX477 120fps ISP delivery using Direct Libargus EGLStream pipeline.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

probe_bin="$REPO_ROOT/build/bin/argus_fps_probe"

if [[ ! -x "$probe_bin" ]]; then
    echo "argus_fps_probe not found, compiling..."
    mkdir -p "$REPO_ROOT/build/bin"
    g++ -O3 "$SCRIPT_DIR/argus_fps_probe.cpp" -o "$probe_bin" \
        -I/usr/src/jetson_multimedia_api/include \
        -L/usr/lib/aarch64-linux-gnu/nvidia -lnvargus -lEGL
fi

stamp="$(date +%Y%m%d-%H%M%S)"
output_dir="$REPO_ROOT/build/test_results"
mkdir -p "$output_dir"
json_out="$output_dir/isp120-$stamp.json"

echo "Running ISP 120fps verification (360 frames)..."
"$probe_bin" --frames 360 --output "$json_out" --exposure-us 4000 --gain 2.0

echo "Verification complete. Results saved to: $json_out"
cat "$json_out"
