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
#include <cmath>
#include <algorithm>

using namespace Argus;
using namespace EGLStream;

struct FrameRecord {
    uint64_t number;
    uint64_t sensor_ts_ns;
    uint64_t host_ts_ns;
    uint64_t exposure_ns;
};

int main(int argc, char **argv) {
    int target_frames = 240;
    uint64_t exposure_us = 4000;
    float analog_gain = 2.0f;
    std::string json_output = "";
    std::string sample_prefix;
    int mode_index = 2;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.empty()) return 2;
        if (arg == "--frames" && i + 1 < argc) {
            target_frames = atoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            json_output = argv[++i];
        } else if (arg == "--exposure-us" && i + 1 < argc) {
            exposure_us = strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--gain" && i + 1 < argc) {
            analog_gain = atof(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string value = argv[++i];
            if (value != "0" && value != "1" && value != "2") return 2;
            mode_index = atoi(value.c_str());
        } else if (arg == "--sample-prefix" && i + 1 < argc) {
            sample_prefix = argv[++i];
        } else if (arg == "--help") {
            puts("Usage: argus_fps_probe --mode 0|1|2 --frames N --output summary.json [--sample-prefix PATH] [--exposure-us 4000] [--gain 2]");
            return 0;
        } else if (arg[0] != '-' && i == 1) {
            target_frames = atoi(arg.c_str());
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", arg.c_str());
            return 2;
        }
    }
    if (mode_index < 0 || mode_index > 2 || target_frames < 2 || target_frames > 100000 ||
        !std::isfinite(analog_gain) || analog_gain < 1 || analog_gain > 22.25 ||
        exposure_us < 13 || exposure_us > 8000) {
        fprintf(stderr, "Invalid mode, frame count, exposure or gain\n");
        return 2;
    }
    const double target_fps = mode_index == 2 ? 120.0 : (mode_index == 1 ? 60.0 : 30.0);
    const uint32_t width = mode_index == 2 ? 1332 : (mode_index == 1 ? 1920 : 3840);
    const uint32_t height = mode_index == 2 ? 990 : (mode_index == 1 ? 1080 : 2160);
    const uint64_t period_ns = static_cast<uint64_t>(1e9 / target_fps);
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
        if (i == static_cast<size_t>(mode_index) && res.width() == width && res.height() == height) {
            mode2 = sensorModes[i];
        }
    }

    if (!mode2) {
        fprintf(stderr, "Requested sensor mode/resolution not found!\n");
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

    if (iEglSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888) != STATUS_OK ||
        iEglSettings->setResolution(Size2D<uint32_t>(width, height)) != STATUS_OK ||
        iEglSettings->setMode(EGL_STREAM_MODE_FIFO) != STATUS_OK ||
        iEglSettings->setFifoLength(16) != STATUS_OK ||
        iEglSettings->setMetadataEnable(true) != STATUS_OK) {
        fprintf(stderr, "Stream configuration failed\n");
        return 1;
    }

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
    UniqueObj<Request> request(iCaptureSession->createRequest(CAPTURE_INTENT_PREVIEW));
    IRequest *iRequest = interface_cast<IRequest>(request);
    if (!iRequest) {
        fprintf(stderr, "Failed to create Request\n");
        return 1;
    }

    if (iRequest->enableOutputStream(outputStream.get()) != STATUS_OK) return 1;
    IAutoControlSettings *autoSettings = interface_cast<IAutoControlSettings>(iRequest->getAutoControlSettings());
    if (!autoSettings || autoSettings->setAwbMode(AWB_MODE_AUTO) != STATUS_OK ||
        autoSettings->setAwbLock(false) != STATUS_OK) { fprintf(stderr, "AWB settings rejected\n"); return 1; }

    ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(request);
    if (!iSourceSettings) {
        fprintf(stderr, "Failed to get ISourceSettings\n");
        return 1;
    }
    if (iSourceSettings->setSensorMode(mode2) != STATUS_OK ||
    // Request 120 fps (8333333 ns frame duration)
        iSourceSettings->setFrameDurationRange(Range<uint64_t>(period_ns, period_ns)) != STATUS_OK ||
    // Configured exposure and gain
        iSourceSettings->setExposureTimeRange(Range<uint64_t>(exposure_us * 1000ULL, exposure_us * 1000ULL)) != STATUS_OK ||
        iSourceSettings->setGainRange(Range<float>(analog_gain, analog_gain)) != STATUS_OK) {
        fprintf(stderr, "Source settings rejected\n");
        return 1;
    }

    // Start repeating request first so producer connects to stream
    printf("Starting repeat capture requests at %.0ffps, AWB AUTO...\n", target_fps);
    if (iCaptureSession->repeat(request.get()) != STATUS_OK) {
        fprintf(stderr, "Failed to start repeat capture\n");
        return 1;
    }

    printf("Waiting for stream connection...\n");
    if (iStream->waitUntilConnected(5000000000ULL) != STATUS_OK) {
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
            return 1;
        }
    }

    bool geometry_ok = true;
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
        uint64_t exposure_ns = 0;
        if (iArgusMeta) {
            CaptureMetadata *meta = iArgusMeta->getMetadata();
            ICaptureMetadata *iMeta = interface_cast<ICaptureMetadata>(meta);
            if (iMeta) {
                sensor_ts = iMeta->getSensorTimestamp();
                exposure_ns = iMeta->getSensorExposureTime();
                if (i == 0 || i == target_frames - 1) {
                    BayerTuple<float> wb = iMeta->getAwbGains();
                    printf("Frame %d AWB CCT=%u gains R/GE/GO/B=%.3f/%.3f/%.3f/%.3f\n",
                        i, iMeta->getAwbCct(), wb.r(), wb.gEven(), wb.gOdd(), wb.b());
                }
            }
        }

        IImage2D *image2d = iFrame ? interface_cast<IImage2D>(iFrame->getImage()) : nullptr;
        geometry_ok = geometry_ok && image2d && image2d->getSize().width() == width &&
                      image2d->getSize().height() == height;
        records.push_back({iFrame ? iFrame->getNumber() : 0, sensor_ts, host_ns, exposure_ns});
    }

    // JPEG sampling is OUTSIDE the timed throughput test, so encoding cannot inflate its cost.
    bool samples_ok = true;
    if (!sample_prefix.empty()) {
        for (int i = 0; i < 3; ++i) {
            UniqueObj<Frame> frame(iFrameConsumer->acquireFrame(2000000000ULL));
            IFrame *f = interface_cast<IFrame>(frame);
            IImageJPEG *jpeg = f ? interface_cast<IImageJPEG>(f->getImage()) : nullptr;
            const std::string path = sample_prefix + "-" + std::to_string(i) + ".jpg";
            if (!jpeg || jpeg->writeJPEG(path.c_str()) != STATUS_OK) samples_ok = false;
        }
    }

    iCaptureSession->stopRepeat();
    const bool idle_ok = iCaptureSession->waitForIdle(5000000000ULL) == STATUS_OK;

    if (records.size() < 2) {
        fprintf(stderr, "Not enough frames captured\n");
        return 1;
    }

    uint64_t first_sensor = records.front().sensor_ts_ns;
    uint64_t last_sensor = records.back().sensor_ts_ns;
    double sensor_sec = last_sensor > first_sensor ? (last_sensor - first_sensor) / 1e9 : 0;
    double sensor_fps = sensor_sec > 0 ? (records.size() - 1) / sensor_sec : 0;

    uint64_t first_host = records.front().host_ts_ns;
    uint64_t last_host = records.back().host_ts_ns;
    double host_sec = last_host > first_host ? (last_host - first_host) / 1e9 : 0;
    double host_fps = host_sec > 0 ? (records.size() - 1) / host_sec : 0;

    printf("\n=== DIRECT ARGUS RESULTS ===\n");
    printf("Captured frames : %zu\n", records.size());
    printf("Sensor duration : %.4f sec\n", sensor_sec);
    printf("Sensor FPS      : %.3f fps\n", sensor_fps);
    printf("Host delivery   : %.3f fps\n", host_fps);

    // Analyze frame numbers and intervals
    int num_jumps = 0;
    int invalid_timestamps = 0, bad_sensor_intervals = 0;
    std::vector<double> host_gaps;
    for (const auto &r : records) if (r.sensor_ts_ns == 0 || r.exposure_ns == 0) ++invalid_timestamps;
    double min_gap_ms = 9999.0, max_gap_ms = 0.0;
    for (size_t i = 1; i < records.size(); ++i) {
        uint64_t num_diff = records[i].number - records[i-1].number;
        if (num_diff != 1) num_jumps++;
        uint64_t dt_ns = (records[i].sensor_ts_ns > records[i-1].sensor_ts_ns)
            ? (records[i].sensor_ts_ns - records[i-1].sensor_ts_ns)
            : 0;
        double gap_ms = dt_ns / 1e6;
        if (gap_ms < min_gap_ms) min_gap_ms = gap_ms;
        if (gap_ms > max_gap_ms) max_gap_ms = gap_ms;
        if (records[i].sensor_ts_ns <= records[i-1].sensor_ts_ns ||
            gap_ms < 0.5 * 1e3 / target_fps || gap_ms > 1.5 * 1e3 / target_fps)
            ++bad_sensor_intervals;
        host_gaps.push_back((records[i].host_ts_ns - records[i-1].host_ts_ns) / 1e6);
    }

    printf("Frame number gaps != 1 : %d\n", num_jumps);
    printf("Min inter-frame delta  : %.2f ms\n", min_gap_ms);
    printf("Max inter-frame delta  : %.2f ms\n", max_gap_ms);

    bool passed = (records.size() == (size_t)target_frames &&
                   std::isfinite(host_fps) && std::isfinite(sensor_fps) &&
                   std::abs(host_fps / target_fps - 1) <= 0.02 &&
                   std::abs(sensor_fps / target_fps - 1) <= 0.02 &&
                   invalid_timestamps == 0 && bad_sensor_intervals == 0 &&
                   num_jumps == 0 && geometry_ok && samples_ok && idle_ok);
    printf("STATUS: %s\n", passed ? "PASS" : "FAIL");

    if (!json_output.empty()) {
        std::ofstream out(json_output);
        std::ofstream csv(json_output + ".frames.csv");
        if (!out.is_open() || !csv.is_open()) { fprintf(stderr, "Cannot write results\n"); return 1; }
        csv << "frame_number,sensor_timestamp_ns,host_monotonic_ns,exposure_ns\n";
        for (const auto &r : records) csv << r.number << ',' << r.sensor_ts_ns << ',' << r.host_ts_ns << ',' << r.exposure_ns << '\n';
        if (out.is_open()) {
            out << "{\n"
                << "  \"frames\": " << records.size() << ",\n"
                << "  \"mode\": " << mode_index << ",\n"
                << "  \"width\": " << width << ",\n"
                << "  \"height\": " << height << ",\n"
                << "  \"target_fps\": " << target_fps << ",\n"
                << "  \"geometry_ok\": " << (geometry_ok ? "true" : "false") << ",\n"
                << "  \"samples_ok\": " << (samples_ok ? "true" : "false") << ",\n"
                << "  \"invalid_timestamps\": " << invalid_timestamps << ",\n"
                << "  \"bad_sensor_intervals\": " << bad_sensor_intervals << ",\n"
                << "  \"host_interval_min_ms\": " << *std::min_element(host_gaps.begin(), host_gaps.end()) << ",\n"
                << "  \"host_interval_max_ms\": " << *std::max_element(host_gaps.begin(), host_gaps.end()) << ",\n"
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
