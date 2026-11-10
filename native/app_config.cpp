#include "app_config.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <sstream>
#include <fstream>
#include <sys/stat.h>

namespace screw_guard {

using json = nlohmann::json;

bool ConfigValidator::validate_roi_bounds(const ScrewRoiConfig& roi,
                                         int canonical_w,
                                         int canonical_h,
                                         std::string& err_msg) {
    if (roi.norm_x < 0.0 || roi.norm_x > 1.0 ||
        roi.norm_y < 0.0 || roi.norm_y > 1.0 ||
        roi.norm_r <= 0.0 || roi.norm_r > 1.0) {
        std::ostringstream oss;
        oss << "ROI id " << roi.id << " (" << roi.name << ") normalized parameters out of range [0, 1]:"
            << " norm_x=" << roi.norm_x << ", norm_y=" << roi.norm_y << ", norm_r=" << roi.norm_r;
        err_msg = oss.str();
        return false;
    }

    int xc = static_cast<int>(std::round(roi.norm_x * (canonical_w - 1)));
    int yc = static_cast<int>(std::round(roi.norm_y * (canonical_h - 1)));
    int r  = static_cast<int>(std::round(roi.norm_r * std::min(canonical_w, canonical_h)));

    if (xc - r < 0 || xc + r >= canonical_w || yc - r < 0 || yc + r >= canonical_h) {
        std::ostringstream oss;
        oss << "ROI id " << roi.id << " (" << roi.name << ") circle [xc=" << xc << ", yc=" << yc
            << ", r=" << r << "] exceeds canonical canvas [" << canonical_w << "x" << canonical_h << "]";
        err_msg = oss.str();
        return false;
    }

    return true;
}

bool ConfigValidator::validate_anchor_bounds(const AnchorConfig& anchor,
                                            int canonical_w,
                                            int canonical_h,
                                            std::string& err_msg) {
    if (!std::isfinite(anchor.norm_x) || !std::isfinite(anchor.norm_y) ||
        !std::isfinite(anchor.norm_w) || !std::isfinite(anchor.norm_h) ||
        anchor.norm_x < 0.0 || anchor.norm_x > 1.0 ||
        anchor.norm_y < 0.0 || anchor.norm_y > 1.0 ||
        anchor.norm_w <= 0.0 || anchor.norm_w > 1.0 ||
        anchor.norm_h <= 0.0 || anchor.norm_h > 1.0) {
        std::ostringstream oss;
        oss << "Anchor normalized parameters out of range: norm_x=" << anchor.norm_x
            << ", norm_y=" << anchor.norm_y << ", norm_w=" << anchor.norm_w << ", norm_h=" << anchor.norm_h;
        err_msg = oss.str();
        return false;
    }

    int wa = static_cast<int>(std::round(anchor.norm_w * canonical_w));
    int ha = static_cast<int>(std::round(anchor.norm_h * canonical_h));
    int xa = static_cast<int>(std::round(anchor.norm_x * (canonical_w - 1) - wa * 0.5));
    int ya = static_cast<int>(std::round(anchor.norm_y * (canonical_h - 1) - ha * 0.5));

    if (wa <= 0 || ha <= 0 || xa < 0 || ya < 0 ||
        xa + wa > canonical_w || ya + ha > canonical_h) {
        std::ostringstream oss;
        oss << "Anchor ROI [xa=" << xa << ", ya=" << ya << ", wa=" << wa << ", ha=" << ha
            << "] exceeds canonical canvas [" << canonical_w << "x" << canonical_h << "]";
        err_msg = oss.str();
        return false;
    }

    return true;
}

bool ConfigValidator::load_file(const std::string& path, AppConfig& out_config,
                                std::string& err_code, std::string& err_msg,
                                std::size_t max_file_bytes) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) { err_code = "CONFIG_IO"; err_msg = "cannot stat config file: " + path; return false; }
    if (st.st_size < 0 || static_cast<std::size_t>(st.st_size) > max_file_bytes) {
        err_code = "CONFIG_TOO_LARGE"; err_msg = "config file exceeds 4096 bytes"; return false;
    }
    std::ifstream f(path, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) { err_code = "CONFIG_IO"; err_msg = "cannot read config file"; return false; }
    return parse_values_json(raw, out_config, err_code, err_msg, 4096);
}

bool ConfigValidator::parse_values_json(const std::string& values_json, AppConfig& out_config,
                                        std::string& err_code, std::string& err_msg,
                                        std::size_t max_profile_bytes) {
    try {
        json root = json::parse(values_json);
        if (!root.is_object()) { err_code = "INVALID_CONFIG_ROOT"; err_msg = "config root must be an object"; return false; }
        // Supervisor stores values as the root object. Accept common API/export wrappers too.
        const json* values = &root;
        if (root.contains("values")) {
            if (!root["values"].is_object()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "'values' must be an object"; return false; }
            values = &root["values"];
        } else if (root.contains("data") && root["data"].is_object() && root["data"].contains("values")) {
            if (!root["data"]["values"].is_object()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "'data.values' must be an object"; return false; }
            values = &root["data"]["values"];
        }
        std::string profile;
        if (values->contains("profile_json")) {
            if (!(*values)["profile_json"].is_string()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "'profile_json' must be a string"; return false; }
            profile = (*values)["profile_json"].get<std::string>();
        } else if (values->contains("profile")) {
            if (!(*values)["profile"].is_object()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "'profile' must be an object"; return false; }
            profile = (*values)["profile"].dump();
        } else { err_code = "MISSING_FIELD"; err_msg = "missing 'profile_json' or 'profile'"; return false; }
        if (profile.empty() || profile.size() > max_profile_bytes) { err_code = "PROFILE_TOO_LARGE"; err_msg = "profile_json must be 1..4096 bytes"; return false; }
        try {
            const json profile_doc = json::parse(profile);
            if (!profile_doc.is_object() || !profile_doc.contains("revision")) { err_code = "MISSING_FIELD"; err_msg = "profile missing 'revision'"; return false; }
        } catch (...) { err_code = "INVALID_PROFILE_JSON"; err_msg = "profile_json is not valid JSON"; return false; }
        if (!values->contains("confidence_threshold") || !(*values)["confidence_threshold"].is_number()) {
            err_code = values->contains("confidence_threshold") ? "INVALID_FIELD_TYPE" : "MISSING_FIELD";
            err_msg = "'confidence_threshold' must be a number"; return false;
        }
        if (!values->contains("max_blocks") || !(*values)["max_blocks"].is_number_integer()) {
            err_code = values->contains("max_blocks") ? "INVALID_FIELD_TYPE" : "MISSING_FIELD";
            err_msg = "'max_blocks' must be an integer"; return false;
        }
        const int64_t max_blocks64 = (*values)["max_blocks"].get<int64_t>();
        if (max_blocks64 < 1 || max_blocks64 > 4) { err_code = "INVALID_MAX_BLOCKS"; err_msg = "max_blocks must be in range [1, 4]"; return false; }
        return parse_app_config(profile, (*values)["confidence_threshold"].get<double>(),
                                static_cast<int>(max_blocks64), out_config, err_code, err_msg);
    } catch (const std::exception& e) {
        err_code = "INVALID_CONFIG_JSON"; err_msg = std::string("config parse error: ") + e.what(); return false;
    }
}

bool ConfigValidator::parse_profile_json(const std::string& json_str,
                                        int max_blocks,
                                        RecipeProfile& out_profile,
                                        std::string& err_code,
                                        std::string& err_msg) {
    if (json_str.empty()) {
        err_code = "INVALID_PROFILE_JSON";
        err_msg = "profile_json string is empty";
        return false;
    }

    try {
        json doc;
        try {
            doc = json::parse(json_str);
        } catch (const std::exception& e) {
            err_code = "INVALID_PROFILE_JSON";
            err_msg = std::string("JSON parse error: ") + e.what();
            return false;
        }

        if (!doc.is_object()) {
            err_code = "INVALID_PROFILE_JSON";
            err_msg = "profile_json root must be a JSON object";
            return false;
        }

        RecipeProfile profile;

        // 1. Revision
        if (doc.contains("revision")) {
            if (!doc["revision"].is_number_integer()) {
                err_code = "INVALID_FIELD_TYPE";
                err_msg = "'revision' must be an integer";
                return false;
            }
            int64_t raw_rev = doc["revision"].get<int64_t>();
            if (raw_rev < 1 || raw_rev > 1000000) {
                err_code = "INVALID_PROFILE_JSON";
                err_msg = "revision must be an integer between 1 and 1000000";
                return false;
            }
            profile.revision = static_cast<int>(raw_rev);
        } else {
            profile.revision = 1;
        }

        // 2. Part name
        if (doc.contains("part_name")) {
            if (!doc["part_name"].is_string()) {
                err_code = "INVALID_FIELD_TYPE";
                err_msg = "'part_name' must be a string";
                return false;
            }
            profile.part_name = doc["part_name"].get<std::string>();
        } else {
            profile.part_name = "default_part";
        }

        // 3. Canonical size - range [30, 1280] x [30, 1280]
        if (doc.contains("canonical_size")) {
            const auto& cs = doc["canonical_size"];
            if (!cs.is_array() || cs.size() != 2 ||
                !cs[0].is_number_integer() || !cs[1].is_number_integer()) {
                err_code = "INVALID_CANONICAL_SIZE";
                err_msg = "canonical_size must be an array of two integers [width, height]";
                return false;
            }
            int64_t raw_w = cs[0].get<int64_t>();
            int64_t raw_h = cs[1].get<int64_t>();
            if (raw_w < 30 || raw_w > 1280 || raw_h < 30 || raw_h > 1280) {
                err_code = "INVALID_CANONICAL_SIZE";
                std::ostringstream oss;
                oss << "canonical_size [" << raw_w << ", " << raw_h
                    << "] out of valid range [30~1280, 30~1280]";
                err_msg = oss.str();
                return false;
            }
            profile.canonical_width = static_cast<int>(raw_w);
            profile.canonical_height = static_cast<int>(raw_h);
        } else {
            profile.canonical_width = 300;
            profile.canonical_height = 200;
        }

        // 4. Min block area
        if (doc.contains("min_block_area")) {
            if (!doc["min_block_area"].is_number()) {
                err_code = "INVALID_FIELD_TYPE";
                err_msg = "'min_block_area' must be a number";
                return false;
            }
            profile.min_block_area = doc["min_block_area"].get<double>();
            if (!std::isfinite(profile.min_block_area) || profile.min_block_area <= 0.0) {
                err_code = "INVALID_PROFILE_JSON";
                err_msg = "'min_block_area' must be positive and finite";
                return false;
            }
        } else {
            profile.min_block_area = 4000.0;
        }

        // 5. expected_blocks constraint: 1 <= expected_blocks <= max_blocks <= 4
        if (!doc.contains("expected_blocks") || !doc["expected_blocks"].is_number_integer()) {
            err_code = "INVALID_PROFILE_BLOCKS_RANGE";
            err_msg = "profile_json missing or invalid integer 'expected_blocks'";
            return false;
        }
        int64_t raw_eb = doc["expected_blocks"].get<int64_t>();
        if (raw_eb < 1 || raw_eb > max_blocks || raw_eb > 4) {
            err_code = "INVALID_PROFILE_BLOCKS_RANGE";
            std::ostringstream oss;
            oss << "Constraint violated: 1 <= expected_blocks (" << raw_eb
                << ") <= max_blocks (" << max_blocks << ") <= 4";
            err_msg = oss.str();
            return false;
        }
        profile.expected_blocks = static_cast<int>(raw_eb);

        // 6. Screw ROIs
        if (!doc.contains("screw_rois") || !doc["screw_rois"].is_array()) {
            err_code = "INVALID_PROFILE_SCREWS_COUNT";
            err_msg = "profile_json missing or invalid array 'screw_rois'";
            return false;
        }

        const auto& rois_arr = doc["screw_rois"];
        size_t rois_count = rois_arr.size();
        if (rois_count < 1 || rois_count > 32) {
            err_code = "INVALID_PROFILE_SCREWS_COUNT";
            std::ostringstream oss;
            oss << "screw_rois count (" << rois_count << ") must be between 1 and 32";
            err_msg = oss.str();
            return false;
        }

        // 7. expected_screws_per_block == screw_rois.size() equality constraint
        if (!doc.contains("expected_screws_per_block") || !doc["expected_screws_per_block"].is_number_integer()) {
            err_code = "INVALID_PROFILE_SCREWS_MISMATCH";
            err_msg = "profile_json missing integer 'expected_screws_per_block'";
            return false;
        }
        int64_t raw_espb = doc["expected_screws_per_block"].get<int64_t>();
        if (raw_espb < 1 || raw_espb > 32 || raw_espb != static_cast<int64_t>(rois_count)) {
            err_code = "INVALID_PROFILE_SCREWS_MISMATCH";
            std::ostringstream oss;
            oss << "Equality constraint violated: expected_screws_per_block ("
                << raw_espb << ") != screw_rois.size() (" << rois_count << ")";
            err_msg = oss.str();
            return false;
        }
        profile.expected_screws_per_block = static_cast<int>(raw_espb);

        // Parse individual ScrewRoiConfig items
        for (size_t i = 0; i < rois_count; ++i) {
            const auto& item = rois_arr[i];
            if (!item.is_object()) {
                err_code = "INVALID_FIELD_TYPE";
                err_msg = "screw_rois item must be an object";
                return false;
            }
            ScrewRoiConfig roi;
            if (item.contains("id")) {
                if (!item["id"].is_number_integer()) {
                    err_code = "INVALID_FIELD_TYPE";
                    err_msg = "screw_roi 'id' must be an integer";
                    return false;
                }
                int64_t raw_id = item["id"].get<int64_t>();
                if (raw_id < 1 || raw_id > 1000) {
                    err_code = "INVALID_SCREW_ID";
                    err_msg = "screw_roi id must be an integer in range [1, 1000]";
                    return false;
                }
                roi.id = static_cast<int>(raw_id);
            } else {
                roi.id = static_cast<int>(i + 1);
            }

            if (item.contains("name")) {
                if (!item["name"].is_string()) {
                    err_code = "INVALID_FIELD_TYPE";
                    err_msg = "screw_roi 'name' must be a string";
                    return false;
                }
                roi.name = item["name"].get<std::string>();
            } else {
                roi.name = "screw_" + std::to_string(roi.id);
            }

            if (!item.contains("norm_x") || !item.contains("norm_y") || !item.contains("norm_r")) {
                err_code = "INVALID_PROFILE_ROI_OUT_OF_BOUNDS";
                err_msg = "screw_roi missing norm_x, norm_y or norm_r";
                return false;
            }
            if (!item["norm_x"].is_number() || !item["norm_y"].is_number() || !item["norm_r"].is_number()) {
                err_code = "INVALID_FIELD_TYPE";
                err_msg = "screw_roi norm_x, norm_y and norm_r must be numbers";
                return false;
            }
            roi.norm_x = item["norm_x"].get<double>();
            roi.norm_y = item["norm_y"].get<double>();
            roi.norm_r = item["norm_r"].get<double>();

            if (!std::isfinite(roi.norm_x) || !std::isfinite(roi.norm_y) || !std::isfinite(roi.norm_r)) {
                err_code = "INVALID_FIELD_TYPE";
                err_msg = "screw_roi coordinates must be finite numbers";
                return false;
            }

            std::string bounds_err;
            if (!validate_roi_bounds(roi, profile.canonical_width, profile.canonical_height, bounds_err)) {
                err_code = "INVALID_PROFILE_ROI_OUT_OF_BOUNDS";
                err_msg = bounds_err;
                return false;
            }
            profile.screw_rois.push_back(roi);
        }

        // 8. Anchor
        if (doc.contains("anchor")) {
            if (!doc["anchor"].is_object()) {
                err_code = "INVALID_FIELD_TYPE";
                err_msg = "'anchor' must be an object";
                return false;
            }
            const auto& a = doc["anchor"];
            if (a.contains("norm_x")) {
                if (!a["norm_x"].is_number()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "anchor norm_x must be number"; return false; }
                profile.anchor.norm_x = a["norm_x"].get<double>();
            }
            if (a.contains("norm_y")) {
                if (!a["norm_y"].is_number()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "anchor norm_y must be number"; return false; }
                profile.anchor.norm_y = a["norm_y"].get<double>();
            }
            if (a.contains("norm_w")) {
                if (!a["norm_w"].is_number()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "anchor norm_w must be number"; return false; }
                profile.anchor.norm_w = a["norm_w"].get<double>();
            }
            if (a.contains("norm_h")) {
                if (!a["norm_h"].is_number()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "anchor norm_h must be number"; return false; }
                profile.anchor.norm_h = a["norm_h"].get<double>();
            }
            if (a.contains("ref_edge_mean")) {
                if (!a["ref_edge_mean"].is_number()) { err_code = "INVALID_FIELD_TYPE"; err_msg = "anchor ref_edge_mean must be number"; return false; }
                profile.anchor.ref_edge_mean = a["ref_edge_mean"].get<double>();
            }

            if (!std::isfinite(profile.anchor.norm_x) || !std::isfinite(profile.anchor.norm_y) ||
                !std::isfinite(profile.anchor.norm_w) || !std::isfinite(profile.anchor.norm_h) ||
                !std::isfinite(profile.anchor.ref_edge_mean)) {
                err_code = "INVALID_FIELD_TYPE";
                err_msg = "anchor coordinates and ref_edge_mean must be finite numbers";
                return false;
            }

            std::string anchor_err;
            if (!validate_anchor_bounds(profile.anchor, profile.canonical_width, profile.canonical_height, anchor_err)) {
                err_code = "INVALID_PROFILE_ANCHOR_OUT_OF_BOUNDS";
                err_msg = anchor_err;
                return false;
            }
        } else {
            // Default anchor
            profile.anchor.norm_x = 0.15;
            profile.anchor.norm_y = 0.50;
            profile.anchor.norm_w = 0.15;
            profile.anchor.norm_h = 0.25;
            profile.anchor.ref_edge_mean = 32.5;
        }

        out_profile = profile;
        err_code = "OK";
        err_msg.clear();
        return true;
    } catch (const nlohmann::json::type_error& e) {
        err_code = "INVALID_FIELD_TYPE";
        err_msg = std::string("JSON field type mismatch: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        err_code = "INVALID_PROFILE_JSON";
        err_msg = std::string("JSON processing exception: ") + e.what();
        return false;
    }
}

bool ConfigValidator::parse_app_config(const std::string& profile_json_str,
                                      double confidence_threshold,
                                      int max_blocks,
                                      AppConfig& out_config,
                                      std::string& err_code,
                                      std::string& err_msg) {
    try {
        if (!std::isfinite(confidence_threshold) || confidence_threshold < 0.50 || confidence_threshold > 0.95) {
            err_code = "INVALID_CONFIDENCE_THRESHOLD";
            std::ostringstream oss;
            oss << "confidence_threshold (" << confidence_threshold << ") must be a finite number in range [0.50, 0.95]";
            err_msg = oss.str();
            return false;
        }

        int64_t raw_mb = max_blocks;
        if (raw_mb < 1 || raw_mb > 4) {
            err_code = "INVALID_MAX_BLOCKS";
            std::ostringstream oss;
            oss << "max_blocks (" << raw_mb << ") must be in range [1, 4]";
            err_msg = oss.str();
            return false;
        }

        RecipeProfile profile;
        if (!parse_profile_json(profile_json_str, max_blocks, profile, err_code, err_msg)) {
            return false;
        }

        out_config.profile = profile;
        out_config.profile_json_raw = profile_json_str;
        out_config.confidence_threshold = confidence_threshold;
        out_config.max_blocks = max_blocks;
        err_code = "OK";
        err_msg.clear();
        return true;
    } catch (const nlohmann::json::type_error& e) {
        err_code = "INVALID_FIELD_TYPE";
        err_msg = std::string("JSON field type mismatch: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        err_code = "INVALID_PROFILE_JSON";
        err_msg = std::string("Exception in parse_app_config: ") + e.what();
        return false;
    }
}

} // namespace screw_guard
