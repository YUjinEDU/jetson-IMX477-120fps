CC ?= gcc
CXX ?= g++
CFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++17 -I/usr/include/opencv4

BIN_DIR := ../../build/bin

OPENCV_LIBS := -l:libopencv_core.so.406 -l:libopencv_imgproc.so.406 -l:libopencv_imgcodecs.so.406 -l:libopencv_highgui.so.406

ARGUS_FLAGS := -I/usr/src/jetson_multimedia_api/include -L/usr/lib/aarch64-linux-gnu/nvidia -lnvargus -lEGL

all: $(BIN_DIR)/capture120 $(BIN_DIR)/color_pipeline $(BIN_DIR)/argus_fps_probe

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/capture120: capture120.c sensor_tables.h rpi_tables.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(BIN_DIR)/color_pipeline: color_pipeline.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(OPENCV_LIBS)

$(BIN_DIR)/argus_fps_probe: ../../tests/hardware/argus_fps_probe.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(ARGUS_FLAGS)

clean:
	rm -f $(BIN_DIR)/capture120 $(BIN_DIR)/color_pipeline $(BIN_DIR)/argus_fps_probe

.PHONY: all clean
