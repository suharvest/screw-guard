#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace screw_guard {

struct AnchorConfig {
    double norm_x = 0.15;
    double norm_y = 0.50;
    double norm_w = 0.15;
    double norm_h = 0.25;
    double ref_edge_mean = 32.5;
};

struct ScrewRoiConfig {
    int id = 1;
    std::string name;
    double norm_x = 0.0;
    double norm_y = 0.0;
    double norm_r = 0.0;
};

struct RecipeProfile {
    int revision = 1;
    std::string part_name = "leg_joint";
    int expected_blocks = 2;
    int expected_screws_per_block = 2;
    int canonical_width = 300;
    int canonical_height = 200;
    double min_block_area = 4000.0;
    AnchorConfig anchor;
    std::vector<ScrewRoiConfig> screw_rois;
};

struct AppConfig {
    RecipeProfile profile;
    std::string profile_json_raw;
    double confidence_threshold = 0.60;
    int max_blocks = 4;
};

class ConfigValidator {
public:
    // Load the supervisor sidecar (or a compatible {values:{...}} wrapper).
    // The file is bounded to max_file_bytes and all runtime values are required.
    static bool load_file(const std::string& path,
                          AppConfig& out_config,
                          std::string& err_code,
                          std::string& err_msg,
                          std::size_t max_file_bytes = 65536);

    static bool parse_values_json(const std::string& values_json,
                                  AppConfig& out_config,
                                  std::string& err_code,
                                  std::string& err_msg,
                                  std::size_t max_profile_bytes = 4096);
    // Parses and validates profile_json string under the context of max_blocks
    // Returns true on success, false on failure with err_code and err_msg populated.
    static bool parse_profile_json(const std::string& json_str,
                                  int max_blocks,
                                  RecipeProfile& out_profile,
                                  std::string& err_code,
                                  std::string& err_msg);

    // Parses full AppConfig (profile_json string + confidence_threshold + max_blocks)
    static bool parse_app_config(const std::string& profile_json_str,
                                double confidence_threshold,
                                int max_blocks,
                                AppConfig& out_config,
                                std::string& err_code,
                                std::string& err_msg);

    // Checks if screw roi coordinates are strictly inside canonical canvas (canonical_w x canonical_h)
    // xc = round(norm_x * (W - 1)), yc = round(norm_y * (H - 1)), r = round(norm_r * min(W, H))
    // asserts: xc - r >= 0 && xc + r < W && yc - r >= 0 && yc + r < H
    static bool validate_roi_bounds(const ScrewRoiConfig& roi,
                                   int canonical_w,
                                   int canonical_h,
                                   std::string& err_msg);

    // Checks if anchor ROI is strictly inside canonical canvas
    static bool validate_anchor_bounds(const AnchorConfig& anchor,
                                      int canonical_w,
                                      int canonical_h,
                                      std::string& err_msg);
};

} // namespace screw_guard
