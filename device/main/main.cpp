#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#include <af/app.h>
#include <debug_stream.h>
#include <ha_mqtt.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "app_config.h"
#include "screw_inspector.h"

int main(int argc, char** argv) {
    std::string config_path = "/userdata/local/apps/screw-guard.config.json";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::puts("Usage: screw-guard [--config PATH]");
            return 0;
        } else {
            std::fprintf(stderr, "Invalid argument: %s\n", argv[i]);
            return 1;
        }
    }

    screw_guard::AppConfig config;
    std::string code, error;
    if (!screw_guard::ConfigValidator::load_file(config_path, config, code, error)) {
        std::fprintf(stderr, "Config %s: %s: %s\n", config_path.c_str(), code.c_str(), error.c_str());
        return 1;
    }

    screw_guard::ScrewInspector inspector(config);

    af::CameraOptions cam_opt;
    cam_opt.width = 1920;
    cam_opt.height = 1080;
    cam_opt.fps = 30;
    cam_opt.physical = false;

    af::StreamOptions stream_opt;
    stream_opt.width = 1920;
    stream_opt.height = 1080;
    stream_opt.fps = 15;

    ha_mqtt::ClientOptions mqtt_opts;
    mqtt_opts.app_id = "screw-guard";
    mqtt_opts.client_id = "recamera-screw-guard";
    mqtt_opts.results_topic = "recamera/screw-guard/results";
    mqtt_opts.legacy_host = "127.0.0.1";
    mqtt_opts.legacy_port = 1883;

    af::App app("screw-guard");
    app.with_camera(cam_opt)
       .with_stream(stream_opt)
       .with_debug_ws(8001)
       .with_rtsp()
       .with_mqtt(mqtt_opts);

    int exit_code = 0;
    int starvation_ticks = 0;
    constexpr int kMaxStarvationTicks = 100; // ~1 second starvation threshold (10ms per idle tick)

    app.on_idle([&] {
        if (++starvation_ticks >= kMaxStarvationTicks) {
            std::fprintf(stderr, "Camera starvation: failed to retrieve frames for %d consecutive intervals\n", starvation_ticks);
            exit_code = 2;
            app.stop();
        }
    });

    int64_t frame_id = 0;
    std::string payload;

    app.on_frame([&](const af::VideoFrame& frame, af::Result& result) {
        starvation_ticks = 0;

        if (!frame.valid() || !frame.cpu_accessible() || frame.data() == nullptr) {
            return;
        }

        try {
            const auto start_proc = std::chrono::steady_clock::now();

            cv::Mat rgb(frame.height, frame.width, CV_8UC3, frame.data(), frame.stride());
            cv::Mat bgr;
            cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

            const double timestamp = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            ++frame_id;

            auto inspect_res = inspector.inspect(bgr, frame_id, "frame_" + std::to_string(frame_id), timestamp);
            inspect_res.applied_revision = config.profile.revision;

            if (app.any_sink_active() || app.debug_clients_count() > 0) {
                payload = inspect_res.to_json();
                result.json = payload; // Published via MQTT sink
                if (app.debug_clients_count() > 0) {
                    // Direct publish to WebSocket /results clients with exact schema required by calib.html
                    debug_stream_publish_result(payload.data(), payload.size());
                }
            }

            const auto end_proc = std::chrono::steady_clock::now();
            result.inference_ms = std::chrono::duration<float, std::milli>(end_proc - start_proc).count();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Frame processing exception: %s\n", e.what());
            exit_code = 3;
            app.stop();
        } catch (...) {
            std::fprintf(stderr, "Unknown frame processing exception\n");
            exit_code = 3;
            app.stop();
        }
    });

    const int rc = app.run();
    return (exit_code != 0) ? exit_code : rc;
}
