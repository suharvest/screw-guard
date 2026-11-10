#include "app_config.h"
#include "screw_inspector.h"
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

using namespace screw_guard;

static void usage(const char* p) {
    std::cerr << "usage: " << p << " --config FILE --image FILE [--validate-only]\n";
}

int main(int argc, char** argv) {
    std::string config, image;
    bool validate_only = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](std::string& dst) { if (i + 1 >= argc) { usage(argv[0]); std::exit(2); } dst = argv[++i]; };
        if (a == "--config" || a == "-c") value(config);
        else if (a == "--image" || a == "-i") value(image);
        else if (a == "--validate-only") validate_only = true;
        else { usage(argv[0]); return 2; }
    }
    if (config.empty()) { usage(argv[0]); return 2; }
    AppConfig cfg; std::string code, msg;
    if (!ConfigValidator::load_file(config, cfg, code, msg)) {
        std::cerr << "CONFIG_ERROR " << code << ": " << msg << '\n'; return 1;
    }
    if (validate_only) {
        std::cout << "{\"valid\":true,\"revision\":" << cfg.profile.revision
                  << ",\"screws\":" << cfg.profile.screw_rois.size() << "}\n";
        return 0;
    }
    if (image.empty()) { usage(argv[0]); return 2; }
    cv::Mat frame = cv::imread(image, cv::IMREAD_COLOR);
    if (frame.empty()) { std::cerr << "IMAGE_ERROR cannot read image\n"; return 1; }
    try {
        ScrewInspector inspector(cfg);
        InspectionResult result = inspector.inspect(frame, 1, "offline-1", 0.0);
        std::cout << result.to_json(2) << '\n';
    } catch (const std::exception& e) {
        std::cerr << "INSPECT_ERROR " << e.what() << '\n'; return 1;
    }
    return 0;
}
