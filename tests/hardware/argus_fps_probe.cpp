/*
 * Direct Argus C++ 120fps probe.
 * Bypasses GStreamer / nvarguscamerasrc entirely to measure raw Argus EGLStream throughput.
 */
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <chrono>
#include <vector>
#include <string>

#include <fstream>

using namespace Argus;
using namespace EGLStream;

struct FrameRecord {
    uint64_t number;
    uint64_t sensor_ts_ns;
    uint64_t host_ts_ns;
};

int main(int argc, char **argv) {
    int target_frames = 240;
    uint64_t exposure_us = 4000;
    float analog_gain = 2.0f;
    std::string json_output = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            target_frames = atoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            json_output = argv[++i];
        } else if (arg == "--exposure-us" && i + 1 < argc) {
            exposure_us = strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--gain" && i + 1 < argc) {
            analog_gain = atof(argv[++i]);
        } else if (arg[0] != '-' && i == 1) {
            target_frames = atoi(arg.c_str());
        }
    }
    printf("Starting Direct Argus FPS Probe (frames=%d, exposure=%luus, gain=%.1fx)...\n",
           target_frames, exposure_us, analog_gain);

    // 1. Create CameraProvider
    UniqueObj<CameraProvider> cameraProvider(CameraProvider::create());
    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider) {
        fprintf(stderr, "Failed to create CameraProvider\n");
        return 1;
    }

    std::vector<CameraDevice*> cameraDevices;
    iCameraProvider->getCameraDevices(&cameraDevices);
    if (cameraDevices.empty()) {
        fprintf(stderr, "No camera devices available\n");
        return 1;
    }

    CameraDevice *cameraDevice = cameraDevices[0];
    ICameraProperties *iCameraProperties = interface_cast<ICameraProperties>(cameraDevice);
    if (!iCameraProperties) {
        fprintf(stderr, "Failed to get camera properties\n");
        return 1;
    }

    std::vector<SensorMode*> sensorModes;
    iCameraProperties->getBasicSensorModes(&sensorModes);
    printf("Available sensor modes: %zu\n", sensorModes.size());

    SensorMode *mode2 = nullptr;
    for (size_t i = 0; i < sensorModes.size(); ++i) {
        ISensorMode *iMode = interface_cast<ISensorMode>(sensorModes[i]);
        Size2D<uint32_t> res = iMode->getResolution();
        Range<uint64_t> durationRange = iMode->getFrameDurationRange();
        double maxFps = 1e9 / durationRange.min();
        printf("  Mode %zu: %ux%u, max fps: %.2f\n", i, res.width(), res.height(), maxFps);
        if (i == 2 || (res.width() == 1332 && res.height() == 990)) {
            mode2 = sensorModes[i];
        }
    }

    if (!mode2) {
        fprintf(stderr, "Sensor mode 2 (1332x990) not found!\n");
        return 1;
    }

    // 2. Create CaptureSession
    UniqueObj<CaptureSession> captureSession(iCameraProvider->createCaptureSession(cameraDevice));
    ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(captureSession);
    if (!iCaptureSession) {
        fprintf(stderr, "Failed to create CaptureSession\n");
        return 1;
    }

    // 3. Create OutputStream
    UniqueObj<OutputStreamSettings> streamSettings(
        iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iEglSettings =
        interface_cast<IEGLOutputStreamSettings>(streamSettings);
    if (!iEglSettings) {
        fprintf(stderr, "Failed to get IEGLOutputStreamSettings\n");
        return 1;
    }

    iEglSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
    iEglSettings->setResolution(Size2D<uint32_t>(1332, 990));
    iEglSettings->setMode(EGL_STREAM_MODE_FIFO);
    iEglSettings->setFifoLength(16);

    UniqueObj<OutputStream> outputStream(iCaptureSession->createOutputStream(streamSettings.get()));
    IEGLOutputStream *iStream = interface_cast<IEGLOutputStream>(outputStream);
    if (!iStream) {
        fprintf(stderr, "Failed to create IEGLOutputStream\n");
        return 1;
    }

    // 4. Create FrameConsumer
    UniqueObj<FrameConsumer> consumer(FrameConsumer::create(outputStream.get()));
    IFrameConsumer *iFrameConsumer = interface_cast<IFrameConsumer>(consumer);
    if (!iFrameConsumer) {
        fprintf(stderr, "Failed to create FrameConsumer\n");
        return 1;
    }

    // 5. Create Request and configure source settings
    UniqueObj<Request> request(iCaptureSession->createRequest());
    IRequest *iRequest = interface_cast<IRequest>(request);
    if (!iRequest) {
        fprintf(stderr, "Failed to create Request\n");
        return 1;
    }

    iRequest->enableOutputStream(outputStream.get());

    ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(request);
    if (!iSourceSettings) {
        fprintf(stderr, "Failed to get ISourceSettings\n");
        return 1;
    }
    iSourceSettings->setSensorMode(mode2);
    // Request 120 fps (8333333 ns frame duration)
    iSourceSettings->setFrameDurationRange(Range<uint64_t>(8333333ULL, 8333333ULL));
    // Configured exposure and gain
    iSourceSettings->setExposureTimeRange(Range<uint64_t>(exposure_us * 1000ULL, exposure_us * 1000ULL));
    iSourceSettings->setGainRange(Range<float>(analog_gain, analog_gain));

    // Start repeating request first so producer connects to stream
    printf("Starting repeat capture requests at 120fps...\n");
    if (iCaptureSession->repeat(request.get()) != STATUS_OK) {
        fprintf(stderr, "Failed to start repeat capture\n");
        return 1;
    }

    printf("Waiting for stream connection...\n");
    if (iStream->waitUntilConnected() != STATUS_OK) {
        fprintf(stderr, "Stream failed to connect\n");
        return 1;
    }
    printf("Producer connected!\n");

    std::vector<FrameRecord> records;
    records.reserve(target_frames + 30);

    // Warmup 30 frames
    printf("Capturing warmup frames...\n");
    for (int i = 0; i < 30; ++i) {
        UniqueObj<Frame> frame(iFrameConsumer->acquireFrame(2000000000ULL));
        if (!frame) {
            fprintf(stderr, "Timeout during warmup frame %d\n", i);
            break;
        }
    }

    printf("Capturing %d measured frames...\n", target_frames);
    for (int i = 0; i < target_frames; ++i) {
        Status status;
        UniqueObj<Frame> frame(iFrameConsumer->acquireFrame(2000000000ULL, &status));
        auto host_now = std::chrono::steady_clock::now();
        uint64_t host_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            host_now.time_since_epoch()).count();

        if (!frame) {
            fprintf(stderr, "Frame acquisition timed out at frame %d, status=%d\n", i, status);
            break;
        }

        IFrame *iFrame = interface_cast<IFrame>(frame);
        IArgusCaptureMetadata *iArgusMeta = interface_cast<IArgusCaptureMetadata>(frame);
        uint64_t sensor_ts = 0;
        if (iArgusMeta) {
            CaptureMetadata *meta = iArgusMeta->getMetadata();
            ICaptureMetadata *iMeta = interface_cast<ICaptureMetadata>(meta);
            if (iMeta) {
                sensor_ts = iMeta->getSensorTimestamp();
            }
        }

        records.push_back({iFrame ? iFrame->getNumber() : 0, sensor_ts, host_ns});
    }

    iCaptureSession->stopRepeat();
    iCaptureSession->waitForIdle();

    if (records.size() < 2) {
        fprintf(stderr, "Not enough frames captured\n");
        return 1;
    }

    uint64_t first_sensor = records.front().sensor_ts_ns;
    uint64_t last_sensor = records.back().sensor_ts_ns;
    double sensor_sec = (last_sensor - first_sensor) / 1e9;
    double sensor_fps = (records.size() - 1) / sensor_sec;

    uint64_t first_host = records.front().host_ts_ns;
    uint64_t last_host = records.back().host_ts_ns;
    double host_sec = (last_host - first_host) / 1e9;
    double host_fps = (records.size() - 1) / host_sec;

    printf("\n=== DIRECT ARGUS RESULTS ===\n");
    printf("Captured frames : %zu\n", records.size());
    printf("Sensor duration : %.4f sec\n", sensor_sec);
    printf("Sensor FPS      : %.3f fps\n", sensor_fps);
    printf("Host delivery   : %.3f fps\n", host_fps);

    // Analyze frame numbers and intervals
    int num_jumps = 0;
    double min_gap_ms = 9999.0, max_gap_ms = 0.0;
    for (size_t i = 1; i < records.size(); ++i) {
        uint64_t num_diff = records[i].number - records[i-1].number;
        if (num_diff != 1) num_jumps++;
        uint64_t dt_ns = (records[i].sensor_ts_ns > 0 && records[i-1].sensor_ts_ns > 0)
            ? (records[i].sensor_ts_ns - records[i-1].sensor_ts_ns)
            : (records[i].host_ts_ns - records[i-1].host_ts_ns);
        double gap_ms = dt_ns / 1e6;
        if (gap_ms < min_gap_ms) min_gap_ms = gap_ms;
        if (gap_ms > max_gap_ms) max_gap_ms = gap_ms;
    }

    printf("Frame number gaps != 1 : %d\n", num_jumps);
    printf("Min inter-frame delta  : %.2f ms\n", min_gap_ms);
    printf("Max inter-frame delta  : %.2f ms\n", max_gap_ms);

    bool passed = (records.size() == (size_t)target_frames &&
                   host_fps >= 119.0 && host_fps <= 121.5 &&
                   num_jumps == 0);
    printf("STATUS: %s\n", passed ? "PASS" : "FAIL");

    if (!json_output.empty()) {
        std::ofstream out(json_output);
        if (out.is_open()) {
            out << "{\n"
                << "  \"frames\": " << records.size() << ",\n"
                << "  \"target_frames\": " << target_frames << ",\n"
                << "  \"exposure_us\": " << exposure_us << ",\n"
                << "  \"analog_gain\": " << analog_gain << ",\n"
                << "  \"sensor_duration_sec\": " << sensor_sec << ",\n"
                << "  \"sensor_fps\": " << sensor_fps << ",\n"
                << "  \"host_delivery_fps\": " << host_fps << ",\n"
                << "  \"frame_gaps\": " << num_jumps << ",\n"
                << "  \"min_interval_ms\": " << min_gap_ms << ",\n"
                << "  \"max_interval_ms\": " << max_gap_ms << ",\n"
                << "  \"passed\": " << (passed ? "true" : "false") << "\n"
                << "}\n";
            out.close();
            printf("Saved summary to %s\n", json_output.c_str());
        }
    }

    return passed ? 0 : 1;
}
