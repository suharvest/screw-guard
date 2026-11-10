#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

namespace screw_guard {

enum class ScrewStatus {
    PRESENT,
    MISSING,
    UNCERTAIN,
    CANNOT_INSPECT
};

enum class BlockStatus {
    PASS,
    DEFECT,
    CANNOT_INSPECT
};

enum class BlockReason {
    OK,
    MISSING_SCREW,
    CANNOT_INSPECT_ORIENTATION,
    CANNOT_INSPECT_ROI_OOB,
    CANNOT_INSPECT_LOW_CONFIDENCE,
    UNINSPECTED_OVER_CAPACITY
};

enum class OrientationStatus {
    CONFIRMED_0_DEG,
    CONFIRMED_180_DEG,
    ORIENTATION_AMBIGUOUS
};

enum class OverallStatus {
    PASS,
    DEFECT,
    CANNOT_INSPECT
};

enum class OverallReason {
    OK,
    DEFECT_UNDER_COUNT,
    DEFECT_OVER_COUNT,
    DEFECT_MISSING_SCREW,
    CANNOT_INSPECT_ORIENTATION,
    CANNOT_INSPECT_ROI_OOB,
    CANNOT_INSPECT_LOW_CONFIDENCE
};

struct ScrewResult {
    int id = 1;
    std::string name;
    ScrewStatus status = ScrewStatus::CANNOT_INSPECT;
    double score = -1.0;
    std::pair<double, double> roi_center_xy = {0.0, 0.0};
};

struct BlockResult {
    int block_id = 1;
    std::vector<double> bbox_xywh = {0.0, 0.0, 0.0, 0.0}; // [x, y, w, h]
    double angle_deg = 0.0;
    OrientationStatus orientation = OrientationStatus::ORIENTATION_AMBIGUOUS;
    BlockStatus status = BlockStatus::CANNOT_INSPECT;
    BlockReason reason = BlockReason::CANNOT_INSPECT_ORIENTATION;
    int expected_screws = 2;
    int actual_screws = 0;
    std::vector<ScrewResult> screws;
};

struct SummaryResult {
    int expected_blocks = 2;
    int actual_blocks = 0;
    int passed_blocks = 0;
    int defect_blocks = 0;
    int cannot_inspect_blocks = 0;
};

struct InspectionResult {
    std::string schema_version = "2.4";
    std::string device_id = "recamera-sg2002-station01";
    std::string app_version = "1.0.0";
    int applied_revision = 1;
    std::string cycle_id = "cyc_001";
    int64_t frame_id = 0;
    double timestamp = 0.0;
    OverallStatus overall_status = OverallStatus::PASS;
    OverallReason overall_reason = OverallReason::OK;
    SummaryResult summary;
    std::vector<BlockResult> blocks;

    std::string to_json(int indent = -1) const;
};

std::string screw_status_to_string(ScrewStatus st);
std::string block_status_to_string(BlockStatus st);
std::string block_reason_to_string(BlockReason r);
std::string orientation_to_string(OrientationStatus ori);
std::string overall_status_to_string(OverallStatus st);
std::string overall_reason_to_string(OverallReason r);

ScrewStatus string_to_screw_status(const std::string& str);
BlockStatus string_to_block_status(const std::string& str);
BlockReason string_to_block_reason(const std::string& str);
OrientationStatus string_to_orientation(const std::string& str);
OverallStatus string_to_overall_status(const std::string& str);
OverallReason string_to_overall_reason(const std::string& str);

} // namespace screw_guard
