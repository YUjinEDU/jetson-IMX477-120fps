#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
g++ -O3 -Wall -Wextra -std=c++17 -I/usr/include/opencv4 color_pipeline.cpp -o color_pipeline -l:libopencv_core.so.406 -l:libopencv_imgproc.so.406 -l:libopencv_imgcodecs.so.406 -l:libopencv_highgui.so.406
