#include "app_config.h"
#include "result_payload.h"
#include "screw_inspector.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <nlohmann/json.hpp>

using namespace screw_guard;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[TEST FAILED] " << __FILE__ << ":" << __LINE__ << ": " << msg << std::endl; \
            std::exit(1); \
        } \
    } while (0)

#define TEST_ASSERT_STR_EQ(a, b, msg) \
    do { \
        if (std::string(a) != std::string(b)) { \
            std::cerr << "[TEST FAILED] " << __FILE__ << ":" << __LINE__ << ": " << msg \
                      << " (expected: '" << (b) << "', got: '" << (a) << "')" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

static bool validate_schema_with_python(const std::string& json_str, const std::string& schema_path) {
    const std::string tmp_json_path = "/tmp/test_sg_payload.json";
    {
        std::ofstream ofs(tmp_json_path);
        ofs << json_str;
    }
    const char* python = std::getenv("SCREW_GUARD_PYTHON");
    const std::string python_bin = (python && *python) ? python : "python3";
    std::string cmd = python_bin + " -c \"import json, jsonschema, sys; "
                      "schema = json.load(open('" + schema_path + "')); "
                      "data = json.load(open('" + tmp_json_path + "')); "
                      "jsonschema.validate(instance=data, schema=schema);\" 2>/dev/null";
    int ret = std::system(cmd.c_str());
    return (ret == 0);
}

#ifndef SCREW_GUARD_SCHEMA_PATH
#define SCREW_GUARD_SCHEMA_PATH "result_schema.json"
#endif
static const std::string SCHEMA_PATH = [] {
    const char* env = std::getenv("SCREW_GUARD_SCHEMA_PATH");
    return std::string(env && *env ? env : SCREW_GUARD_SCHEMA_PATH);
}();

static const std::string DEFAULT_VALID_PROFILE =
    "{\"revision\":1,\"part_name\":\"leg_joint\",\"expected_blocks\":2,\"expected_screws_per_block\":2,"
    "\"canonical_size\":[300,200],\"min_block_area\":4000,"
    "\"anchor\":{\"norm_x\":0.15,\"norm_y\":0.5,\"norm_w\":0.15,\"norm_h\":0.25,\"ref_edge_mean\":32.5},"
    "\"screw_rois\":["
    "{\"id\":1,\"name\":\"侧面M2\",\"norm_x\":0.283,\"norm_y\":0.700,\"norm_r\":0.080},"
    "{\"id\":2,\"name\":\"关节M2\",\"norm_x\":0.600,\"norm_y\":0.450,\"norm_r\":0.080}"
    "]}";

// =========================================================================
// Test 1: AppConfig and RecipeProfile validation constraints
// =========================================================================
static void test_app_config_validation() {
    std::cout << "[RUN] test_app_config_validation..." << std::endl;

    AppConfig config;
    std::string err_code, err_msg;

    // 1.1 Valid profile
    bool ok = ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, config, err_code, err_msg);
    TEST_ASSERT(ok, "Default profile parsing should succeed");
    TEST_ASSERT_STR_EQ(err_code, "OK", "err_code should be OK");
    TEST_ASSERT(config.profile.expected_blocks == 2, "expected_blocks should be 2");
    TEST_ASSERT(config.profile.expected_screws_per_block == 2, "expected_screws_per_block should be 2");
    TEST_ASSERT(config.profile.screw_rois.size() == 2, "screw_rois size should be 2");

    // 1.2 Constraint: 1 <= expected_blocks <= max_blocks <= 4
    {
        // expected_blocks = 0
        std::string json_zero_blocks = "{\"expected_blocks\":0,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_zero_blocks, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "expected_blocks=0 should fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_BLOCKS_RANGE", "Should return INVALID_PROFILE_BLOCKS_RANGE");

        // expected_blocks = 5
        std::string json_five_blocks = "{\"expected_blocks\":5,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_five_blocks, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "expected_blocks=5 should fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_BLOCKS_RANGE", "Should return INVALID_PROFILE_BLOCKS_RANGE");

        // expected_blocks = 3, max_blocks = 2
        std::string json_three_blocks = "{\"expected_blocks\":3,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_three_blocks, 0.60, 2, config, err_code, err_msg);
        TEST_ASSERT(!ok, "expected_blocks > max_blocks should fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_BLOCKS_RANGE", "Should return INVALID_PROFILE_BLOCKS_RANGE");
    }

    // 1.3 Equality constraint: expected_screws_per_block == screw_rois.size()
    {
        // expected_screws_per_block = 2, screw_rois.size() = 1
        std::string json_mismatch = "{\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_mismatch, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "Mismatch between expected_screws and rois.size() must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_SCREWS_MISMATCH", "Should return INVALID_PROFILE_SCREWS_MISMATCH");

        // screw_rois.size() = 33 (violates 1 <= screw_rois.size() <= 32)
        std::ostringstream oss_33;
        oss_33 << "{\"expected_blocks\":2,\"expected_screws_per_block\":33,\"screw_rois\":[";
        for (int i = 1; i <= 33; ++i) {
            oss_33 << "{\"id\":" << i << ",\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.01}" << (i < 33 ? "," : "");
        }
        oss_33 << "]}";
        ok = ConfigValidator::parse_app_config(oss_33.str(), 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "screw_rois.size() == 33 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_SCREWS_COUNT", "Should return INVALID_PROFILE_SCREWS_COUNT");

        // screw_rois.size() = 4 succeeds (supports 4+ screws)
        std::string json_four_rois = "{\"expected_blocks\":2,\"expected_screws_per_block\":4,"
            "\"screw_rois\":["
            "{\"id\":1,\"norm_x\":0.2,\"norm_y\":0.3,\"norm_r\":0.05},"
            "{\"id\":2,\"norm_x\":0.2,\"norm_y\":0.7,\"norm_r\":0.05},"
            "{\"id\":3,\"norm_x\":0.6,\"norm_y\":0.3,\"norm_r\":0.05},"
            "{\"id\":4,\"norm_x\":0.6,\"norm_y\":0.7,\"norm_r\":0.05}]}";
        ok = ConfigValidator::parse_app_config(json_four_rois, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(ok, "screw_rois.size() == 4 must succeed");
        TEST_ASSERT(config.profile.expected_screws_per_block == 4, "expected_screws_per_block should be 4");

        // screw_rois.size() = 0
        std::string json_zero_rois = "{\"expected_blocks\":2,\"expected_screws_per_block\":0,\"screw_rois\":[]}";
        ok = ConfigValidator::parse_app_config(json_zero_rois, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "screw_rois.size() == 0 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_SCREWS_COUNT", "Should return INVALID_PROFILE_SCREWS_COUNT");
    }

    // 1.4 Canvas boundary check (norm_x, norm_y, norm_r on 300x200 canvas)
    {
        // norm_x = 0.0, norm_r = 0.08 -> xc - r < 0
        std::string json_oob_left = "{\"expected_blocks\":1,\"expected_screws_per_block\":1,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.0,\"norm_y\":0.5,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_oob_left, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "ROI exceeding left canvas boundary must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_ROI_OUT_OF_BOUNDS", "Should return INVALID_PROFILE_ROI_OUT_OF_BOUNDS");

        // norm_y = 0.98, norm_r = 0.08 -> yc + r >= 200
        std::string json_oob_bottom = "{\"expected_blocks\":1,\"expected_screws_per_block\":1,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.98,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_oob_bottom, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "ROI exceeding bottom canvas boundary must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_ROI_OUT_OF_BOUNDS", "Should return INVALID_PROFILE_ROI_OUT_OF_BOUNDS");
    }

    // 1.5 Confidence threshold boundaries
    {
        ok = ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.45, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "confidence_threshold < 0.50 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CONFIDENCE_THRESHOLD", "Should return INVALID_CONFIDENCE_THRESHOLD");

        ok = ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.98, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "confidence_threshold > 0.95 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CONFIDENCE_THRESHOLD", "Should return INVALID_CONFIDENCE_THRESHOLD");
    }

    // 1.6 Max blocks boundaries
    {
        ok = ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 0, config, err_code, err_msg);
        TEST_ASSERT(!ok, "max_blocks < 1 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_MAX_BLOCKS", "Should return INVALID_MAX_BLOCKS");

        ok = ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 5, config, err_code, err_msg);
        TEST_ASSERT(!ok, "max_blocks > 4 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_MAX_BLOCKS", "Should return INVALID_MAX_BLOCKS");
    }

    // 1.7 NaN / Inf confidence threshold
    {
        double nan_thresh = std::numeric_limits<double>::quiet_NaN();
        ok = ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, nan_thresh, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "NaN confidence_threshold must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CONFIDENCE_THRESHOLD", "Should return INVALID_CONFIDENCE_THRESHOLD");

        double inf_thresh = std::numeric_limits<double>::infinity();
        ok = ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, inf_thresh, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "Inf confidence_threshold must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CONFIDENCE_THRESHOLD", "Should return INVALID_CONFIDENCE_THRESHOLD");
    }

    // 1.8 canonical_size range [30, 1280] x [30, 1280] and arbitrary aspect ratios
    {
        // Width < 30 must fail
        std::string json_too_narrow = "{\"canonical_size\":[29,200],\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_too_narrow, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "canonical_size width < 30 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CANONICAL_SIZE", "Should return INVALID_CANONICAL_SIZE");

        // Height < 30 must fail
        std::string json_too_short = "{\"canonical_size\":[200,29],\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_too_short, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "canonical_size height < 30 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CANONICAL_SIZE", "Should return INVALID_CANONICAL_SIZE");

        // Width > 1280 must fail
        std::string json_too_wide = "{\"canonical_size\":[1281,200],\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_too_wide, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "canonical_size width > 1280 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CANONICAL_SIZE", "Should return INVALID_CANONICAL_SIZE");

        // Height > 1280 must fail
        std::string json_too_tall = "{\"canonical_size\":[300,1281],\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_too_tall, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "canonical_size height > 1280 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CANONICAL_SIZE", "Should return INVALID_CANONICAL_SIZE");

        // Non-array must fail
        std::string json_not_array = "{\"canonical_size\":\"300x200\",\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_not_array, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "canonical_size non-array must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CANONICAL_SIZE", "Should return INVALID_CANONICAL_SIZE");

        // Array with != 2 elements must fail
        std::string json_three_elems = "{\"canonical_size\":[300,200,100],\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_three_elems, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "canonical_size size != 2 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CANONICAL_SIZE", "Should return INVALID_CANONICAL_SIZE");

        // [400, 300] (4:3 ratio, non 3:2) must pass
        std::string json_400_300 = "{\"canonical_size\":[400,300],\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_400_300, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(ok, "canonical_size [400, 300] (non-3:2) must pass");
        TEST_ASSERT(config.profile.canonical_width == 400 && config.profile.canonical_height == 300,
                    "Canonical width/height should be 400x300");

        // Boundary values: [30, 30] and [1280, 1280] must pass
        std::string json_min_bound = "{\"canonical_size\":[30,30],\"expected_blocks\":1,\"expected_screws_per_block\":1,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.1}]}";
        ok = ConfigValidator::parse_app_config(json_min_bound, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(ok, "canonical_size [30, 30] must pass");

        std::string json_max_bound = "{\"canonical_size\":[1280,1280],\"expected_blocks\":1,\"expected_screws_per_block\":1,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.5,\"norm_y\":0.5,\"norm_r\":0.05}]}";
        ok = ConfigValidator::parse_app_config(json_max_bound, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(ok, "canonical_size [1280, 1280] must pass");
    }

    // 1.9 screw_rois[i].id >= 1
    {
        std::string json_zero_id = "{\"expected_blocks\":1,\"expected_screws_per_block\":1,"
            "\"screw_rois\":[{\"id\":0,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_zero_id, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "screw_rois id=0 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_SCREW_ID", "Should return INVALID_SCREW_ID");

        std::string json_neg_id = "{\"expected_blocks\":1,\"expected_screws_per_block\":1,"
            "\"screw_rois\":[{\"id\":-2,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_neg_id, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "screw_rois id < 0 must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_SCREW_ID", "Should return INVALID_SCREW_ID");
    }

    // 1.10 Malformed / null field types
    {
        std::string json_null_field = "{\"expected_blocks\":null,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_null_field, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "expected_blocks=null must fail");

        std::string json_null_rois = "{\"expected_blocks\":2,\"expected_screws_per_block\":2,\"screw_rois\":null}";
        ok = ConfigValidator::parse_app_config(json_null_rois, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "screw_rois=null must fail");

        std::string json_str_norm = "{\"expected_blocks\":1,\"expected_screws_per_block\":1,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":\"not_a_number\",\"norm_y\":0.7,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_str_norm, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "norm_x string must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_FIELD_TYPE", "Should return INVALID_FIELD_TYPE");
    }

    // 1.11 ScrewInspector constructor validation
    {
        AppConfig invalid_cfg;
        invalid_cfg.confidence_threshold = 0.20; // invalid threshold < 0.50
        invalid_cfg.max_blocks = 4;
        bool caught = false;
        try {
            ScrewInspector bad_inspector(invalid_cfg);
        } catch (const std::invalid_argument& e) {
            caught = true;
        }
        TEST_ASSERT(caught, "ScrewInspector constructor with threshold 0.20 must throw invalid_argument");
    }

    // 1.12 Degenerate size defense in disambiguate_orientation
    {
        AppConfig valid_cfg;
        ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, valid_cfg, err_code, err_msg);
        ScrewInspector inspector(valid_cfg);
        cv::Mat small_orig(1080, 1920, CV_8UC1, cv::Scalar(50));
        cv::RotatedRect tiny_rect(cv::Point2f(500, 500), cv::Size2f(3.0f, 2.0f), 0.0f);
        cv::Mat M, M_inv;
        float angle = 0.0f;
        OrientationDecision dec = inspector.disambiguate_orientation(small_orig, tiny_rect, M, M_inv, angle);
        TEST_ASSERT(!dec.is_valid, "Degenerate rect (<5px) must return is_valid=false");
        TEST_ASSERT(dec.orientation == OrientationStatus::ORIENTATION_AMBIGUOUS,
                    "Degenerate rect must return ORIENTATION_AMBIGUOUS");
    }

    // 1.13 Score rounding and state decision alignment
    {
        AppConfig valid_cfg;
        ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, valid_cfg, err_code, err_msg);
        ScrewInspector inspector(valid_cfg);

        ScrewRoiConfig roi;
        roi.id = 1;
        roi.norm_x = 0.5;
        roi.norm_y = 0.5;
        roi.norm_r = 0.08;
        cv::Mat M_inv = (cv::Mat_<double>(2, 3) << 1.0, 0.0, 100.0, 0.0, 1.0, 100.0);
        cv::Mat full_img(1080, 1920, CV_8UC1, cv::Scalar(80));
        ScrewResult res = inspector.evaluate_screw(full_img, roi, M_inv, 1920, 1080, 0.60);
        TEST_ASSERT(res.status == ScrewStatus::MISSING, "Smooth patch should be MISSING");
        TEST_ASSERT(res.score == 0.0, "Smooth patch score should be 0.0");
    }

    // 1.14 64-bit integer overflow narrowing rejection (Major 1)
    {
        // canonical_size width = 4294967596 (2^32 + 300)
        std::string json_overflow_w = "{\"canonical_size\":[4294967596,200],\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_overflow_w, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "64-bit overflow canonical width must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_CANONICAL_SIZE", "Should return INVALID_CANONICAL_SIZE");

        // screw id = 4294967297 (2^32 + 1)
        std::string json_overflow_id = "{\"expected_blocks\":1,\"expected_screws_per_block\":1,"
            "\"screw_rois\":[{\"id\":4294967297,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_overflow_id, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "64-bit overflow screw id must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_SCREW_ID", "Should return INVALID_SCREW_ID");

        // expected_blocks = 4294967298 (2^32 + 2)
        std::string json_overflow_eb = "{\"expected_blocks\":4294967298,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_overflow_eb, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "64-bit overflow expected_blocks must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_BLOCKS_RANGE", "Should return INVALID_PROFILE_BLOCKS_RANGE");

        // revision = 4294967297 (2^32 + 1)
        std::string json_overflow_rev = "{\"revision\":4294967297,\"expected_blocks\":2,\"expected_screws_per_block\":2,"
            "\"screw_rois\":[{\"id\":1,\"norm_x\":0.283,\"norm_y\":0.7,\"norm_r\":0.08},"
            "{\"id\":2,\"norm_x\":0.6,\"norm_y\":0.45,\"norm_r\":0.08}]}";
        ok = ConfigValidator::parse_app_config(json_overflow_rev, 0.60, 4, config, err_code, err_msg);
        TEST_ASSERT(!ok, "64-bit overflow revision must fail");
        TEST_ASSERT_STR_EQ(err_code, "INVALID_PROFILE_JSON", "Should return INVALID_PROFILE_JSON");
    }

    // 1.15 Strict Weak Ordering comparator verification (Major 2)
    {
        // Test points including A=(0,202), B=(1.5,101), C=(3,0), D=(0,50), E=(1.5,50)
        std::vector<cv::RotatedRect> rects = {
            cv::RotatedRect(cv::Point2f(0.0f, 202.0f), cv::Size2f(10, 10), 0.0f),
            cv::RotatedRect(cv::Point2f(1.5f, 101.0f), cv::Size2f(10, 10), 0.0f),
            cv::RotatedRect(cv::Point2f(3.0f, 0.0f), cv::Size2f(10, 10), 0.0f),
            cv::RotatedRect(cv::Point2f(0.0f, 50.0f), cv::Size2f(10, 10), 0.0f),
            cv::RotatedRect(cv::Point2f(1.5f, 50.0f), cv::Size2f(10, 10), 0.0f)
        };

        auto cmp = [](const cv::RotatedRect& a, const cv::RotatedRect& b) {
            if (a.center.x != b.center.x) {
                return a.center.x < b.center.x;
            }
            return a.center.y < b.center.y;
        };

        std::sort(rects.begin(), rects.end(), cmp);

        // Verification of strict (x, y) lexicographical ordering
        TEST_ASSERT(rects[0].center.x == 0.0f && rects[0].center.y == 50.0f, "Rect 0 must be (0, 50)");
        TEST_ASSERT(rects[1].center.x == 0.0f && rects[1].center.y == 202.0f, "Rect 1 must be (0, 202)");
        TEST_ASSERT(rects[2].center.x == 1.5f && rects[2].center.y == 50.0f, "Rect 2 must be (1.5, 50)");
        TEST_ASSERT(rects[3].center.x == 1.5f && rects[3].center.y == 101.0f, "Rect 3 must be (1.5, 101)");
        TEST_ASSERT(rects[4].center.x == 3.0f && rects[4].center.y == 0.0f, "Rect 4 must be (3, 0)");

        // Strict Weak Ordering axioms test
        for (size_t i = 0; i < rects.size(); ++i) {
            TEST_ASSERT(!cmp(rects[i], rects[i]), "Irreflexivity violated");
            for (size_t j = 0; j < rects.size(); ++j) {
                if (cmp(rects[i], rects[j])) {
                    TEST_ASSERT(!cmp(rects[j], rects[i]), "Asymmetry violated");
                }
                for (size_t k = 0; k < rects.size(); ++k) {
                    if (cmp(rects[i], rects[j]) && cmp(rects[j], rects[k])) {
                        TEST_ASSERT(cmp(rects[i], rects[k]), "Transitivity violated");
                    }
                }
            }
        }
    }

    // 1.16 Score rounding boundary tests (0.499 vs 0.500 and 0.349 vs 0.350)
    {
        double conf = 0.50;
        auto decide = [conf](double raw_score) {
            double s = std::clamp(raw_score, 0.0, 1.0);
            s = std::round(s * 100.0) / 100.0;
            if (s >= conf) return ScrewStatus::PRESENT;
            if (s < 0.35) return ScrewStatus::MISSING;
            return ScrewStatus::UNCERTAIN;
        };

        TEST_ASSERT(decide(0.499) == ScrewStatus::PRESENT, "0.499 rounded to 0.50 must be PRESENT with thresh=0.50");
        TEST_ASSERT(decide(0.494) == ScrewStatus::UNCERTAIN, "0.494 rounded to 0.49 must be UNCERTAIN with thresh=0.50");
        TEST_ASSERT(decide(0.349) == ScrewStatus::UNCERTAIN, "0.349 rounded to 0.35 must be UNCERTAIN");
        TEST_ASSERT(decide(0.344) == ScrewStatus::MISSING, "0.344 rounded to 0.34 must be MISSING");
    }

    std::cout << "[PASS] test_app_config_validation passed." << std::endl;
}

// =========================================================================
// Synthetic Image Generation Helpers for CV Tests
// =========================================================================
struct SyntheticScrewDef {
    int xc;
    int yc;
    bool present = true;
    bool uncertain = false;
};

struct SyntheticBlockDef {
    cv::Point2f center{0.0f, 0.0f};
    float angle = 0.0f;
    bool is_180_deg = false;
    bool anchor_asymmetric = true;
    bool corrupt_anchor_nan = false;
    bool screw1_present = true;
    bool screw2_present = true;
    bool screw1_uncertain = false;
    bool screw2_uncertain = false;
    int s1_xc = 85;
    int s1_yc = 139;
    int s2_xc = 179;
    int s2_yc = 90;
    int canon_w = 300;
    int canon_h = 200;
    std::vector<SyntheticScrewDef> custom_screws{};

    SyntheticBlockDef() = default;

    SyntheticBlockDef(cv::Point2f c,
                      float a = 0.0f,
                      bool is_180 = false,
                      bool a_asym = true,
                      bool c_nan = false,
                      bool s1_p = true,
                      bool s2_p = true,
                      bool s1_u = false,
                      bool s2_u = false,
                      int s1x = 85,
                      int s1y = 139,
                      int s2x = 179,
                      int s2y = 90,
                      int cw = 300,
                      int ch = 200,
                      std::vector<SyntheticScrewDef> cs = {})
        : center(c), angle(a), is_180_deg(is_180), anchor_asymmetric(a_asym),
          corrupt_anchor_nan(c_nan), screw1_present(s1_p), screw2_present(s2_p),
          screw1_uncertain(s1_u), screw2_uncertain(s2_u),
          s1_xc(s1x), s1_yc(s1y), s2_xc(s2x), s2_yc(s2y),
          canon_w(cw), canon_h(ch), custom_screws(std::move(cs)) {}
};

static cv::Mat generate_synthetic_scene(int img_w, int img_h, const std::vector<SyntheticBlockDef>& blocks) {
    // Background: dark gray 20
    cv::Mat scene(img_h, img_w, CV_8UC3, cv::Scalar(20, 20, 20));

    for (const auto& blk : blocks) {
        int cw = blk.canon_w > 0 ? blk.canon_w : 300;
        int ch = blk.canon_h > 0 ? blk.canon_h : 200;

        // Construct canonical canvas for this block
        cv::Mat canon(ch, cw, CV_8UC3, cv::Scalar(80, 80, 80)); // Workpiece body

        // Draw border for clear contour
        cv::rectangle(canon, cv::Rect(0, 0, cw, ch), cv::Scalar(110, 110, 110), 2);

        // Anchor ROI (norm_x=0.15, norm_y=0.50, norm_w=0.15, norm_h=0.25)
        // Unified contract: norm_x, norm_y are center of anchor ROI
        int wa = static_cast<int>(std::round(0.15 * cw));
        int ha = static_cast<int>(std::round(0.25 * ch));
        int xa = static_cast<int>(std::round(0.15 * (cw - 1) - wa * 0.5));
        int ya = static_cast<int>(std::round(0.50 * (ch - 1) - ha * 0.5));

        int anchor_x = blk.is_180_deg ? (cw - xa - wa) : xa;
        int anchor_y = blk.is_180_deg ? (ch - ya - ha) : ya;

        if (blk.anchor_asymmetric) {
            // Draw asymmetric notch/feature inside anchor region (contrast to achieve Sobel gradient ~ 32.5, no pixels > 165)
            for (int y = anchor_y + 4; y < anchor_y + ha - 4; y += 4) {
                cv::line(canon, cv::Point(anchor_x + 4, y), cv::Point(anchor_x + wa - 8, y), cv::Scalar(135, 135, 135), 2);
            }
        }
        // If anchor_asymmetric is false, both anchor regions remain smooth (em ~ 0), causing S0=0, S180=0 -> AMBIGUOUS

        auto draw_screw_patch = [](cv::Mat& img, int xc, int yc, bool present, bool uncertain) {
            if (present && !uncertain) {
                // Highly textured screw head with bright reflection (>165)
                cv::circle(img, cv::Point(xc, yc), 18, cv::Scalar(150, 150, 150), -1);
                // Cross slot
                cv::line(img, cv::Point(xc - 12, yc), cv::Point(xc + 12, yc), cv::Scalar(30, 30, 30), 3);
                cv::line(img, cv::Point(xc, yc - 12), cv::Point(xc, yc + 12), cv::Scalar(30, 30, 30), 3);
                // Specular reflection center highlight (> 165)
                cv::circle(img, cv::Point(xc + 3, yc - 3), 7, cv::Scalar(240, 240, 240), -1);
            } else if (uncertain) {
                // Moderate texture, no specular reflection > 165
                cv::circle(img, cv::Point(xc, yc), 18, cv::Scalar(100, 100, 100), -1);
                cv::line(img, cv::Point(xc - 8, yc), cv::Point(xc + 8, yc), cv::Scalar(40, 40, 40), 2);
            } else {
                // Missing: smooth dark empty hole, low edge energy (<10), no specular reflection
                cv::circle(img, cv::Point(xc, yc), 16, cv::Scalar(55, 55, 55), -1);
            }
        };

        if (!blk.custom_screws.empty()) {
            for (const auto& sc : blk.custom_screws) {
                int s_xc = blk.is_180_deg ? ((cw - 1) - sc.xc) : sc.xc;
                int s_yc = blk.is_180_deg ? ((ch - 1) - sc.yc) : sc.yc;
                draw_screw_patch(canon, s_xc, s_yc, sc.present, sc.uncertain);
            }
        } else {
            // Screws in canonical coordinates:
            // Screw 1: norm_x=0.283, norm_y=0.700 -> xc=85, yc=139
            // Screw 2: norm_x=0.600, norm_y=0.450 -> xc=179, yc=90
            int s1_xc = blk.is_180_deg ? (cw - 1 - blk.s1_xc) : blk.s1_xc;
            int s1_yc = blk.is_180_deg ? (ch - 1 - blk.s1_yc) : blk.s1_yc;
            int s2_xc = blk.is_180_deg ? (cw - 1 - blk.s2_xc) : blk.s2_xc;
            int s2_yc = blk.is_180_deg ? (ch - 1 - blk.s2_yc) : blk.s2_yc;
            draw_screw_patch(canon, s1_xc, s1_yc, blk.screw1_present, blk.screw1_uncertain);
            draw_screw_patch(canon, s2_xc, s2_yc, blk.screw2_present, blk.screw2_uncertain);
        }

        // Warp canonical canvas into scene
        cv::Point2f src[3] = {
            cv::Point2f(cw * 0.5f, ch * 0.5f),
            cv::Point2f(static_cast<float>(cw), ch * 0.5f),
            cv::Point2f(cw * 0.5f, static_cast<float>(ch))
        };

        float rad = blk.angle * static_cast<float>(CV_PI) / 180.0f;
        float ux = std::cos(rad);
        float uy = std::sin(rad);
        float vx = -std::sin(rad);
        float vy = std::cos(rad);

        cv::Point2f dst[3] = {
            blk.center,
            blk.center + cv::Point2f(ux * (cw * 0.5f), uy * (cw * 0.5f)),
            blk.center + cv::Point2f(vx * (ch * 0.5f), vy * (ch * 0.5f))
        };

        cv::Mat M = cv::getAffineTransform(src, dst);
        cv::Mat warped;
        cv::warpAffine(canon, warped, M, cv::Size(img_w, img_h), cv::INTER_LINEAR);

        // Mask warped workpiece onto scene
        cv::Mat gray_w, mask;
        cv::cvtColor(warped, gray_w, cv::COLOR_BGR2GRAY);
        cv::threshold(gray_w, mask, 10, 255, cv::THRESH_BINARY);
        warped.copyTo(scene, mask);
    }

    return scene;
}

// =========================================================================
// Test 2: Normal Pass (All blocks present, all screws present)
// =========================================================================
static void test_normal_pass() {
    std::cout << "[RUN] test_normal_pass..." << std::endl;

    AppConfig config;
    std::string err_code, err_msg;
    bool ok = ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, config, err_code, err_msg);
    TEST_ASSERT(ok, "Config parse should succeed");

    ScrewInspector inspector(config);

    // 2 blocks: Block 1 at 0 deg, Block 2 at 180 deg
    std::vector<SyntheticBlockDef> defs = {
        { cv::Point2f(400.0f, 500.0f), 0.0f, false, true, false, true, true, false, false },
        { cv::Point2f(1100.0f, 500.0f), 10.0f, true, true, false, true, true, false, false }
    };
    cv::Mat scene = generate_synthetic_scene(1920, 1080, defs);

    InspectionResult res = inspector.inspect(scene, 101, "cyc_001", 1000.0);
    std::cout << "[DEBUG test_normal_pass payload]:\n" << res.to_json(2) << std::endl;

    TEST_ASSERT_STR_EQ(res.overall_status == OverallStatus::PASS ? "PASS" : "FAIL", "PASS", "Overall status must be PASS");
    TEST_ASSERT_STR_EQ(overall_reason_to_string(res.overall_reason), "OK", "Overall reason must be OK");
    TEST_ASSERT(res.summary.expected_blocks == 2, "Expected 2 blocks");
    TEST_ASSERT(res.summary.actual_blocks == 2, "Actual 2 blocks");
    TEST_ASSERT(res.summary.passed_blocks == 2, "Passed 2 blocks");
    TEST_ASSERT(res.summary.defect_blocks == 0, "0 defect blocks");
    TEST_ASSERT(res.summary.cannot_inspect_blocks == 0, "0 cannot inspect blocks");
    TEST_ASSERT(res.blocks.size() == 2, "2 blocks in result");

    // Check Block 1
    TEST_ASSERT(res.blocks[0].status == BlockStatus::PASS, "Block 1 must pass");
    TEST_ASSERT(res.blocks[0].orientation == OrientationStatus::CONFIRMED_0_DEG, "Block 1 must be 0_DEG_CONFIRMED");
    TEST_ASSERT(res.blocks[0].actual_screws == 2, "Block 1 actual_screws == 2");
    TEST_ASSERT(res.blocks[0].screws[0].status == ScrewStatus::PRESENT, "Block 1 screw 1 PRESENT");
    TEST_ASSERT(res.blocks[0].screws[1].status == ScrewStatus::PRESENT, "Block 1 screw 2 PRESENT");

    // Check Block 2 (180 deg inverted)
    TEST_ASSERT(res.blocks[1].status == BlockStatus::PASS, "Block 2 must pass");
    TEST_ASSERT(res.blocks[1].orientation == OrientationStatus::CONFIRMED_180_DEG, "Block 2 must be 180_DEG_CONFIRMED");
    TEST_ASSERT(res.blocks[1].actual_screws == 2, "Block 2 actual_screws == 2");
    TEST_ASSERT(res.blocks[1].screws[0].status == ScrewStatus::PRESENT, "Block 2 screw 1 PRESENT");
    TEST_ASSERT(res.blocks[1].screws[1].status == ScrewStatus::PRESENT, "Block 2 screw 2 PRESENT");

    // JSON Schema validation
    std::string json_str = res.to_json();
    TEST_ASSERT(validate_schema_with_python(json_str, SCHEMA_PATH), "Result JSON must strictly conform to Draft-07 schema");

    std::cout << "[PASS] test_normal_pass passed." << std::endl;
}

// =========================================================================
// Test 3: Missing Screw (DEFECT)
// =========================================================================
static void test_missing_screw() {
    std::cout << "[RUN] test_missing_screw..." << std::endl;

    AppConfig config;
    std::string err_code, err_msg;
    ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, config, err_code, err_msg);
    ScrewInspector inspector(config);

    // Block 1 good, Block 2 missing screw 1
    std::vector<SyntheticBlockDef> defs = {
        { cv::Point2f(400.0f, 500.0f), 0.0f, false, true, false, true, true, false, false },
        { cv::Point2f(1100.0f, 500.0f), 0.0f, false, true, false, false, true, false, false } // screw 1 missing
    };
    cv::Mat scene = generate_synthetic_scene(1920, 1080, defs);

    InspectionResult res = inspector.inspect(scene, 102, "cyc_002", 1001.0);
    std::cout << "[DEBUG test_missing_screw payload]:\n" << res.to_json(2) << std::endl;

    TEST_ASSERT(res.overall_status == OverallStatus::DEFECT, "Overall status must be DEFECT");
    TEST_ASSERT_STR_EQ(overall_reason_to_string(res.overall_reason), "DEFECT_MISSING_SCREW", "Overall reason must be DEFECT_MISSING_SCREW");
    TEST_ASSERT(res.summary.passed_blocks == 1, "Passed blocks == 1");
    TEST_ASSERT(res.summary.defect_blocks == 1, "Defect blocks == 1");

    // Block 2 details
    TEST_ASSERT(res.blocks[1].status == BlockStatus::DEFECT, "Block 2 must be DEFECT");
    TEST_ASSERT_STR_EQ(block_reason_to_string(res.blocks[1].reason), "MISSING_SCREW", "Block 2 reason must be MISSING_SCREW");
    TEST_ASSERT(res.blocks[1].screws[0].status == ScrewStatus::MISSING, "Screw 1 must be MISSING");
    TEST_ASSERT(res.blocks[1].screws[0].score < 0.35, "Missing screw score must be < 0.35");
    TEST_ASSERT(res.blocks[1].screws[1].status == ScrewStatus::PRESENT, "Screw 2 must be PRESENT");
    TEST_ASSERT(res.blocks[1].actual_screws == 1, "actual_screws should be 1");

    // JSON Schema validation
    std::string json_str = res.to_json();
    TEST_ASSERT(validate_schema_with_python(json_str, SCHEMA_PATH), "Result JSON must strictly conform to Draft-07 schema");

    std::cout << "[PASS] test_missing_screw passed." << std::endl;
}

// =========================================================================
// Test 4: Orientation Ambiguity (CANNOT_INSPECT_ORIENTATION)
// =========================================================================
static void test_orientation_ambiguous() {
    std::cout << "[RUN] test_orientation_ambiguous..." << std::endl;

    AppConfig config;
    std::string err_code, err_msg;
    ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, config, err_code, err_msg);
    ScrewInspector inspector(config);

    // Symmetric/flat anchor pattern -> ambiguous orientation
    std::vector<SyntheticBlockDef> defs = {
        { cv::Point2f(400.0f, 500.0f), 0.0f, false, true, false, true, true, false, false },
        { cv::Point2f(1100.0f, 500.0f), 0.0f, false, false, false, true, true, false, false } // ambiguous anchor
    };
    cv::Mat scene = generate_synthetic_scene(1920, 1080, defs);

    InspectionResult res = inspector.inspect(scene, 103, "cyc_003", 1002.0);
    std::cout << "[DEBUG test_orientation_ambiguous payload]:\n" << res.to_json(2) << std::endl;

    TEST_ASSERT(res.overall_status == OverallStatus::CANNOT_INSPECT, "Overall status must be CANNOT_INSPECT");
    TEST_ASSERT_STR_EQ(overall_reason_to_string(res.overall_reason), "CANNOT_INSPECT_ORIENTATION",
                       "Overall reason must be CANNOT_INSPECT_ORIENTATION");

    TEST_ASSERT(res.blocks[1].status == BlockStatus::CANNOT_INSPECT, "Block 2 must be CANNOT_INSPECT");
    TEST_ASSERT_STR_EQ(block_reason_to_string(res.blocks[1].reason), "CANNOT_INSPECT_ORIENTATION",
                       "Block 2 reason must be CANNOT_INSPECT_ORIENTATION");
    TEST_ASSERT_STR_EQ(orientation_to_string(res.blocks[1].orientation), "ORIENTATION_AMBIGUOUS",
                       "Orientation must be ORIENTATION_AMBIGUOUS");
    TEST_ASSERT(res.blocks[1].actual_screws == 0, "actual_screws must be 0 for ambiguous orientation");
    TEST_ASSERT(res.blocks[1].screws.empty(), "screws array should be empty when orientation is ambiguous");

    // JSON Schema validation
    std::string json_str = res.to_json();
    TEST_ASSERT(validate_schema_with_python(json_str, SCHEMA_PATH), "Result JSON must strictly conform to Draft-07 schema");

    std::cout << "[PASS] test_orientation_ambiguous passed." << std::endl;
}

// =========================================================================
// Test 5: Patch Out of Bounds Defense (ValidPatch 防御)
// =========================================================================
static void test_patch_out_of_bounds() {
    std::cout << "[RUN] test_patch_out_of_bounds..." << std::endl;

    // Profile where screw 2 is placed near the right edge: norm_x=0.940, norm_y=0.450 -> xc=281, yc=90
    // Canvas boundary check: xc=281, r=round(0.04*200)=8 -> xc+r=289 < 300, xc-r=273 >= 0. (Valid profile!)
    const std::string OOB_PROFILE =
        "{\"revision\":1,\"part_name\":\"leg_joint_edge\",\"expected_blocks\":2,\"expected_screws_per_block\":2,"
        "\"canonical_size\":[300,200],\"min_block_area\":4000,"
        "\"anchor\":{\"norm_x\":0.15,\"norm_y\":0.5,\"norm_w\":0.15,\"norm_h\":0.25,\"ref_edge_mean\":32.5},"
        "\"screw_rois\":["
        "{\"id\":1,\"name\":\"侧面M2\",\"norm_x\":0.283,\"norm_y\":0.700,\"norm_r\":0.080},"
        "{\"id\":2,\"name\":\"边缘M2\",\"norm_x\":0.940,\"norm_y\":0.450,\"norm_r\":0.040}"
        "]}";

    AppConfig config;
    std::string err_code, err_msg;
    bool ok = ConfigValidator::parse_app_config(OOB_PROFILE, 0.60, 4, config, err_code, err_msg);
    TEST_ASSERT(ok, "OOB profile parse should succeed");
    ScrewInspector inspector(config);

    // Block 1: normal at (400, 500)
    // Block 2: center at (1770, 500)
    // Block 2 width: 300 (from 1620 to 1918 < 1920). Block is completely inside image!
    // Screw 2 in Block 2: xc=281 -> X_orig = 1770 + (281 - 150) = 1901.
    // Patch check: X_orig + 24 = 1901 + 24 = 1925 >= 1920 -> ValidPatch defense triggers!
    SyntheticBlockDef def1 = { cv::Point2f(400.0f, 500.0f), 0.0f, false, true, false, true, true, false, false, 85, 139, 281, 90 };
    SyntheticBlockDef def2 = { cv::Point2f(1770.0f, 500.0f), 0.0f, false, true, false, true, true, false, false, 85, 139, 281, 90 };

    std::vector<SyntheticBlockDef> defs = { def1, def2 };
    cv::Mat scene = generate_synthetic_scene(1920, 1080, defs);

    InspectionResult res = inspector.inspect(scene, 104, "cyc_004", 1003.0);
    std::cout << "[DEBUG test_patch_out_of_bounds payload]:\n" << res.to_json(2) << std::endl;

    TEST_ASSERT(res.overall_status == OverallStatus::CANNOT_INSPECT, "Overall status must be CANNOT_INSPECT");
    TEST_ASSERT_STR_EQ(overall_reason_to_string(res.overall_reason), "CANNOT_INSPECT_ROI_OOB",
                       "Overall reason must be CANNOT_INSPECT_ROI_OOB");

    TEST_ASSERT(res.blocks[1].status == BlockStatus::CANNOT_INSPECT, "Block 2 must be CANNOT_INSPECT");
    TEST_ASSERT_STR_EQ(block_reason_to_string(res.blocks[1].reason), "CANNOT_INSPECT_ROI_OOB",
                       "Block 2 reason must be CANNOT_INSPECT_ROI_OOB");

    // Check screw 2 status and score
    TEST_ASSERT(res.blocks[1].screws[1].status == ScrewStatus::CANNOT_INSPECT, "Screw 2 status must be CANNOT_INSPECT");
    TEST_ASSERT(res.blocks[1].screws[1].score == -1.0, "Screw 2 score must be -1.0 when patch is out of bounds");

    // JSON Schema validation
    std::string json_str = res.to_json();
    TEST_ASSERT(validate_schema_with_python(json_str, SCHEMA_PATH), "Result JSON must strictly conform to Draft-07 schema");

    std::cout << "[PASS] test_patch_out_of_bounds passed." << std::endl;
}

// =========================================================================
// Test 6: Under-Count (欠件 DEFECT_UNDER_COUNT)
// =========================================================================
static void test_under_count() {
    std::cout << "[RUN] test_under_count..." << std::endl;

    AppConfig config;
    std::string err_code, err_msg;
    // Expected 2 blocks
    ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, config, err_code, err_msg);
    ScrewInspector inspector(config);

    // Only 1 block in scene!
    std::vector<SyntheticBlockDef> defs = {
        { cv::Point2f(600.0f, 500.0f), 0.0f, false, true, false, true, true, false, false }
    };
    cv::Mat scene = generate_synthetic_scene(1920, 1080, defs);

    InspectionResult res = inspector.inspect(scene, 105, "cyc_005", 1004.0);

    TEST_ASSERT(res.overall_status == OverallStatus::DEFECT, "Under count must be DEFECT");
    TEST_ASSERT_STR_EQ(overall_reason_to_string(res.overall_reason), "DEFECT_UNDER_COUNT",
                       "Overall reason must be DEFECT_UNDER_COUNT");
    TEST_ASSERT(res.summary.expected_blocks == 2, "Expected 2 blocks");
    TEST_ASSERT(res.summary.actual_blocks == 1, "Actual 1 block");
    TEST_ASSERT(res.blocks.size() == 1, "1 block in payload");

    // JSON Schema validation
    std::string json_str = res.to_json();
    TEST_ASSERT(validate_schema_with_python(json_str, SCHEMA_PATH), "Result JSON must strictly conform to Draft-07 schema");

    std::cout << "[PASS] test_under_count passed." << std::endl;
}

// =========================================================================
// Test 7: Over-Count and Over-Capacity (超量 DEFECT_OVER_COUNT & UNINSPECTED_OVER_CAPACITY)
// =========================================================================
static void test_over_count_and_capacity() {
    std::cout << "[RUN] test_over_count_and_capacity..." << std::endl;

    AppConfig config;
    std::string err_code, err_msg;
    // Expected 2 blocks, max_blocks = 4
    ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, config, err_code, err_msg);
    ScrewInspector inspector(config);

    // 5 blocks in scene (exceeds max_blocks=4)
    std::vector<SyntheticBlockDef> defs = {
        { cv::Point2f(200.0f, 500.0f), 0.0f, false, true, false, true, true, false, false },
        { cv::Point2f(550.0f, 500.0f), 0.0f, false, true, false, true, true, false, false },
        { cv::Point2f(900.0f, 500.0f), 0.0f, false, true, false, true, true, false, false },
        { cv::Point2f(1250.0f, 500.0f), 0.0f, false, true, false, true, true, false, false },
        { cv::Point2f(1600.0f, 500.0f), 0.0f, false, true, false, true, true, false, false }
    };
    cv::Mat scene = generate_synthetic_scene(1920, 1080, defs);

    InspectionResult res = inspector.inspect(scene, 106, "cyc_006", 1005.0);

    TEST_ASSERT(res.overall_status == OverallStatus::DEFECT, "Over count must be DEFECT");
    TEST_ASSERT_STR_EQ(overall_reason_to_string(res.overall_reason), "DEFECT_OVER_COUNT",
                       "Overall reason must be DEFECT_OVER_COUNT");
    TEST_ASSERT(res.summary.expected_blocks == 2, "Expected 2 blocks");
    TEST_ASSERT(res.summary.actual_blocks == 5, "Actual blocks recorded accurately as 5");
    TEST_ASSERT(res.blocks.size() == 5, "All 5 blocks must be recorded in blocks array");

    // Blocks 1..4 are inspected
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT(res.blocks[i].status == BlockStatus::PASS, "Blocks 1..4 should be inspected and pass");
    }

    // Block 5 (index 4) exceeds max_blocks=4
    // Specification requirements (§4.3 item 2):
    // status: CANNOT_INSPECT, reason: UNINSPECTED_OVER_CAPACITY, orientation: ORIENTATION_AMBIGUOUS,
    // expected_screws: expected_screws_per_block, actual_screws: 0, screws: []
    const auto& b5 = res.blocks[4];
    TEST_ASSERT(b5.status == BlockStatus::CANNOT_INSPECT, "Block 5 status must be CANNOT_INSPECT");
    TEST_ASSERT_STR_EQ(block_reason_to_string(b5.reason), "UNINSPECTED_OVER_CAPACITY",
                       "Block 5 reason must be UNINSPECTED_OVER_CAPACITY");
    TEST_ASSERT_STR_EQ(orientation_to_string(b5.orientation), "ORIENTATION_AMBIGUOUS",
                       "Block 5 orientation must be ORIENTATION_AMBIGUOUS");
    TEST_ASSERT(b5.expected_screws == 2, "Block 5 expected_screws must match profile");
    TEST_ASSERT(b5.actual_screws == 0, "Block 5 actual_screws must be 0");
    TEST_ASSERT(b5.screws.empty(), "Block 5 screws array must be empty []");

    TEST_ASSERT(res.summary.passed_blocks == 4, "Passed blocks == 4");
    TEST_ASSERT(res.summary.cannot_inspect_blocks == 1, "cannot_inspect_blocks == 1 (block 5)");

    // JSON Schema validation
    std::string json_str = res.to_json();
    TEST_ASSERT(validate_schema_with_python(json_str, SCHEMA_PATH), "Result JSON must strictly conform to Draft-07 schema");

    std::cout << "[PASS] test_over_count_and_capacity passed." << std::endl;
}

// =========================================================================
// Test 8: Four Screws Multi-Block Inspection (4孔多工件检测: 4在位 vs 3在1缺)
// =========================================================================
static void test_four_screws_multi_block() {
    std::cout << "[RUN] test_four_screws_multi_block..." << std::endl;

    const std::string FOUR_SCREWS_PROFILE =
        "{\"revision\":2,\"part_name\":\"quad_mount\",\"expected_blocks\":2,\"expected_screws_per_block\":4,"
        "\"canonical_size\":[400,300],\"min_block_area\":4000,"
        "\"anchor\":{\"norm_x\":0.15,\"norm_y\":0.5,\"norm_w\":0.15,\"norm_h\":0.25,\"ref_edge_mean\":32.5},"
        "\"screw_rois\":["
        "{\"id\":1,\"name\":\"M2_TL\",\"norm_x\":0.35,\"norm_y\":0.30,\"norm_r\":0.06},"
        "{\"id\":2,\"name\":\"M2_TR\",\"norm_x\":0.75,\"norm_y\":0.30,\"norm_r\":0.06},"
        "{\"id\":3,\"name\":\"M2_BL\",\"norm_x\":0.35,\"norm_y\":0.70,\"norm_r\":0.06},"
        "{\"id\":4,\"name\":\"M2_BR\",\"norm_x\":0.75,\"norm_y\":0.70,\"norm_r\":0.06}"
        "]}";

    AppConfig config;
    std::string err_code, err_msg;
    bool ok = ConfigValidator::parse_app_config(FOUR_SCREWS_PROFILE, 0.60, 4, config, err_code, err_msg);
    TEST_ASSERT(ok, "Four-screws profile parse must succeed");
    TEST_ASSERT(config.profile.expected_screws_per_block == 4, "expected_screws_per_block == 4");
    TEST_ASSERT(config.profile.canonical_width == 400, "canonical_width == 400");
    TEST_ASSERT(config.profile.canonical_height == 300, "canonical_height == 300");

    ScrewInspector inspector(config);

    // Coordinate calculation on 400x300 canonical canvas:
    // s1: norm_x=0.35 * 399 = 140, norm_y=0.30 * 299 = 90
    // s2: norm_x=0.75 * 399 = 299, norm_y=0.30 * 299 = 90
    // s3: norm_x=0.35 * 399 = 140, norm_y=0.70 * 299 = 209
    // s4: norm_x=0.75 * 399 = 299, norm_y=0.70 * 299 = 209
    std::vector<SyntheticScrewDef> block1_screws = {
        { 140, 90,  true, false },  // TL: present
        { 299, 90,  true, false },  // TR: present
        { 140, 209, true, false },  // BL: present
        { 299, 209, true, false }   // BR: present
    };

    std::vector<SyntheticScrewDef> block2_screws = {
        { 140, 90,  true, false },  // TL: present
        { 299, 90,  true, false },  // TR: present
        { 140, 209, true, false },  // BL: present
        { 299, 209, false, false }  // BR: MISSING! (3 present, 1 missing)
    };

    // 2 blocks in scene:
    // Block 1 at (450, 500), 0 deg, all 4 screws present
    // Block 2 at (1200, 500), 0 deg, 3 present + 1 missing
    SyntheticBlockDef b1;
    b1.center = cv::Point2f(450.0f, 500.0f);
    b1.angle = 0.0f;
    b1.is_180_deg = false;
    b1.anchor_asymmetric = true;
    b1.canon_w = 400;
    b1.canon_h = 300;
    b1.custom_screws = block1_screws;

    SyntheticBlockDef b2;
    b2.center = cv::Point2f(1200.0f, 500.0f);
    b2.angle = 0.0f;
    b2.is_180_deg = false;
    b2.anchor_asymmetric = true;
    b2.canon_w = 400;
    b2.canon_h = 300;
    b2.custom_screws = block2_screws;

    cv::Mat scene = generate_synthetic_scene(1920, 1080, { b1, b2 });

    InspectionResult res = inspector.inspect(scene, 108, "cyc_008", 1007.0);

    std::string json_str = res.to_json(2);
    std::cout << "[DEBUG test_four_screws_multi_block payload]:\n" << json_str << std::endl;

    TEST_ASSERT(res.overall_status == OverallStatus::DEFECT, "Overall status must be DEFECT due to missing screw in block 2");
    TEST_ASSERT_STR_EQ(overall_reason_to_string(res.overall_reason), "DEFECT_MISSING_SCREW",
                       "Overall reason must be DEFECT_MISSING_SCREW");
    TEST_ASSERT(res.summary.expected_blocks == 2, "Expected blocks == 2");
    TEST_ASSERT(res.summary.actual_blocks == 2, "Actual blocks == 2");
    TEST_ASSERT(res.summary.passed_blocks == 1, "Passed blocks == 1");
    TEST_ASSERT(res.summary.defect_blocks == 1, "Defect blocks == 1");
    TEST_ASSERT(res.summary.cannot_inspect_blocks == 0, "Cannot inspect blocks == 0");
    TEST_ASSERT(res.blocks.size() == 2, "2 blocks in result");

    // Block 1: all 4 screws present -> PASS
    const auto& res_b1 = res.blocks[0];
    TEST_ASSERT(res_b1.status == BlockStatus::PASS, "Block 1 must PASS");
    TEST_ASSERT_STR_EQ(block_reason_to_string(res_b1.reason), "OK", "Block 1 reason OK");
    TEST_ASSERT(res_b1.expected_screws == 4, "Block 1 expected_screws == 4");
    TEST_ASSERT(res_b1.actual_screws == 4, "Block 1 actual_screws == 4");
    TEST_ASSERT(res_b1.screws.size() == 4, "Block 1 has 4 screw results");
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT(res_b1.screws[i].status == ScrewStatus::PRESENT, "Block 1 screw must be PRESENT");
        TEST_ASSERT(res_b1.screws[i].score >= 0.60, "Block 1 screw score >= 0.60");
    }

    // Block 2: 3 present, 1 missing -> DEFECT
    const auto& res_b2 = res.blocks[1];
    TEST_ASSERT(res_b2.status == BlockStatus::DEFECT, "Block 2 must be DEFECT");
    TEST_ASSERT_STR_EQ(block_reason_to_string(res_b2.reason), "MISSING_SCREW", "Block 2 reason MISSING_SCREW");
    TEST_ASSERT(res_b2.expected_screws == 4, "Block 2 expected_screws == 4");
    TEST_ASSERT(res_b2.actual_screws == 3, "Block 2 actual_screws == 3");
    TEST_ASSERT(res_b2.screws.size() == 4, "Block 2 has 4 screw results");
    TEST_ASSERT(res_b2.screws[0].status == ScrewStatus::PRESENT, "Block 2 screw 1 present");
    TEST_ASSERT(res_b2.screws[1].status == ScrewStatus::PRESENT, "Block 2 screw 2 present");
    TEST_ASSERT(res_b2.screws[2].status == ScrewStatus::PRESENT, "Block 2 screw 3 present");
    TEST_ASSERT(res_b2.screws[3].status == ScrewStatus::MISSING, "Block 2 screw 4 MISSING");
    TEST_ASSERT(res_b2.screws[3].score < 0.35, "Block 2 missing screw score < 0.35");

    // JSON Schema validation
    TEST_ASSERT(validate_schema_with_python(res.to_json(), SCHEMA_PATH), "Result JSON with 4 screws must conform to schema");

    std::cout << "[PASS] test_four_screws_multi_block passed." << std::endl;
}

static void test_nonuniform_resize_geometry() {
    AppConfig cfg; std::string code, msg;
    TEST_ASSERT(ConfigValidator::parse_app_config(DEFAULT_VALID_PROFILE, 0.60, 4, cfg, code, msg), "geometry config");
    ScrewInspector inspector(cfg);
    for (float angle : {0.0f, 27.0f}) {
        cv::Mat scene(1100, 1400, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::RotatedRect expected(cv::Point2f(700, 550), cv::Size2f(600, 300), angle);
        cv::Point2f corners[4]; expected.points(corners);
        std::vector<cv::Point> polygon; for (auto p : corners) polygon.push_back(p);
        cv::fillConvexPoly(scene, polygon, cv::Scalar(80, 100, 130));
        auto boxes = inspector.coarse_detect_blocks(scene);
        TEST_ASSERT(boxes.size() == 1, "one geometry box");
        float width = std::max(boxes[0].size.width, boxes[0].size.height);
        float height = std::min(boxes[0].size.width, boxes[0].size.height);
        std::cerr << "[EVIDENCE] geometry angle=" << angle << " center=" << boxes[0].center << " dimensions=" << width << "x" << height << " detected_angle=" << boxes[0].angle << "\n";
        TEST_ASSERT(cv::norm(boxes[0].center - expected.center) < 5, "geometry center within 5 pixels");
        TEST_ASSERT(std::abs(width - 600) < 8 && std::abs(height - 300) < 8, "geometry dimensions within 8 pixels");
        float observed = boxes[0].angle; if (boxes[0].size.width < boxes[0].size.height) observed += 90;
        TEST_ASSERT(std::abs(std::remainder(observed - angle, 180.0f)) < 1, "geometry angle within 1 degree");
    }
}

static void test_vertical_and_32_screw_profiles() {
    std::cout << "[RUN] test_vertical_and_32_screw_profiles..." << std::endl;
    for (bool vertical : {false, true}) for (int count : {2, 32}) {
        int w = vertical ? 800 : 1000, h = vertical ? 1000 : 800;
        nlohmann::json profile = {
            {"revision", 2}, {"expected_blocks", 1}, {"expected_screws_per_block", count},
            {"canonical_size", {w, h}},
            {"anchor", {{"norm_x", 0.15}, {"norm_y", 0.5}, {"norm_w", 0.15}, {"norm_h", 0.25}, {"ref_edge_mean", 8.0}}},
            {"screw_rois", nlohmann::json::array()}
        };
        SyntheticBlockDef block; block.center = cv::Point2f(700, 650); block.canon_w = w; block.canon_h = h;
        for (int i = 0; i < count; ++i) {
            int x = vertical ? 210 + 50 * (i % 8) : 260 + 70 * (i % 8);
            int y = vertical ? 180 + 200 * (i / 8) : 140 + 160 * (i / 8);
            profile["screw_rois"].push_back({{"id", i+1}, {"norm_x", double(x)/(w-1)}, {"norm_y", double(y)/(h-1)}, {"norm_r", 0.025}});
            block.custom_screws.push_back({x, y, true, false});
        }
        AppConfig cfg; std::string code, msg;
        TEST_ASSERT(ConfigValidator::parse_app_config(profile.dump(), 0.60, 4, cfg, code, msg), "profile parses");
        ScrewInspector inspector(cfg);
        for (bool missing : {false, true}) {
            block.custom_screws.back().present = !missing;
            auto result = inspector.inspect(generate_synthetic_scene(1400, 1300, {block}), 32, "grid", 1.0);
            TEST_ASSERT(result.blocks.size() == 1 && result.summary.actual_blocks == 1, "one actual block");
            const auto& b = result.blocks[0];
            std::cerr << "[EVIDENCE] vertical=" << vertical << " count=" << count << " missing=" << missing << " status=" << block_status_to_string(b.status) << " actual=" << b.actual_screws << " orientation=" << orientation_to_string(b.orientation) << "\n";
            TEST_ASSERT(b.orientation == OrientationStatus::CONFIRMED_0_DEG, "confirmed orientation");
            TEST_ASSERT(b.expected_screws == count && b.actual_screws == count - int(missing), "exact screw counts");
            TEST_ASSERT(b.screws.size() == size_t(count), "exact screw records");
            TEST_ASSERT(b.status == (missing ? BlockStatus::DEFECT : BlockStatus::PASS), "exact block status");
            TEST_ASSERT(result.overall_status == (missing ? OverallStatus::DEFECT : OverallStatus::PASS), "exact overall status");
            for (int i = 0; i < count; ++i) {
                TEST_ASSERT(b.screws[i].status == ((missing && i == count-1) ? ScrewStatus::MISSING : ScrewStatus::PRESENT), "exact screw presence");
                auto xy = b.screws[i].roi_center_xy;
                TEST_ASSERT(std::abs(xy.first - (700-w/2+block.custom_screws[i].xc)) < 5 &&
                            std::abs(xy.second - (650-h/2+block.custom_screws[i].yc)) < 5, "source screw coordinates within 5 pixels");
            }
            TEST_ASSERT(validate_schema_with_python(result.to_json(), SCHEMA_PATH), "grid output schema");
        }
    }
    std::cout << "[PASS] test_vertical_and_32_screw_profiles passed." << std::endl;
}

// =========================================================================
// Main Entry
// =========================================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running ScrewGuard Native Unit Tests   " << std::endl;
    std::cout << "========================================" << std::endl;

    test_app_config_validation();
    test_normal_pass();
    test_missing_screw();
    test_orientation_ambiguous();
    test_patch_out_of_bounds();
    test_under_count();
    test_over_count_and_capacity();
    test_four_screws_multi_block();
    test_nonuniform_resize_geometry();
    test_vertical_and_32_screw_profiles();

    std::cout << "========================================" << std::endl;
    std::cout << " All 8 test suites PASSED successfully! " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
