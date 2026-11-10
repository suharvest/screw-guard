#include "result_payload.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace screw_guard {

using json = nlohmann::json;

std::string screw_status_to_string(ScrewStatus st) {
    switch (st) {
        case ScrewStatus::PRESENT: return "PRESENT";
        case ScrewStatus::MISSING: return "MISSING";
        case ScrewStatus::UNCERTAIN: return "UNCERTAIN";
        case ScrewStatus::CANNOT_INSPECT: return "CANNOT_INSPECT";
    }
    return "CANNOT_INSPECT";
}

std::string block_status_to_string(BlockStatus st) {
    switch (st) {
        case BlockStatus::PASS: return "PASS";
        case BlockStatus::DEFECT: return "DEFECT";
        case BlockStatus::CANNOT_INSPECT: return "CANNOT_INSPECT";
    }
    return "CANNOT_INSPECT";
}

std::string block_reason_to_string(BlockReason r) {
    switch (r) {
        case BlockReason::OK: return "OK";
        case BlockReason::MISSING_SCREW: return "MISSING_SCREW";
        case BlockReason::CANNOT_INSPECT_ORIENTATION: return "CANNOT_INSPECT_ORIENTATION";
        case BlockReason::CANNOT_INSPECT_ROI_OOB: return "CANNOT_INSPECT_ROI_OOB";
        case BlockReason::CANNOT_INSPECT_LOW_CONFIDENCE: return "CANNOT_INSPECT_LOW_CONFIDENCE";
        case BlockReason::UNINSPECTED_OVER_CAPACITY: return "UNINSPECTED_OVER_CAPACITY";
    }
    return "CANNOT_INSPECT_ORIENTATION";
}

std::string orientation_to_string(OrientationStatus ori) {
    switch (ori) {
        case OrientationStatus::CONFIRMED_0_DEG: return "0_DEG_CONFIRMED";
        case OrientationStatus::CONFIRMED_180_DEG: return "180_DEG_CONFIRMED";
        case OrientationStatus::ORIENTATION_AMBIGUOUS: return "ORIENTATION_AMBIGUOUS";
    }
    return "ORIENTATION_AMBIGUOUS";
}

std::string overall_status_to_string(OverallStatus st) {
    switch (st) {
        case OverallStatus::PASS: return "PASS";
        case OverallStatus::DEFECT: return "DEFECT";
        case OverallStatus::CANNOT_INSPECT: return "CANNOT_INSPECT";
    }
    return "CANNOT_INSPECT";
}

std::string overall_reason_to_string(OverallReason r) {
    switch (r) {
        case OverallReason::OK: return "OK";
        case OverallReason::DEFECT_UNDER_COUNT: return "DEFECT_UNDER_COUNT";
        case OverallReason::DEFECT_OVER_COUNT: return "DEFECT_OVER_COUNT";
        case OverallReason::DEFECT_MISSING_SCREW: return "DEFECT_MISSING_SCREW";
        case OverallReason::CANNOT_INSPECT_ORIENTATION: return "CANNOT_INSPECT_ORIENTATION";
        case OverallReason::CANNOT_INSPECT_ROI_OOB: return "CANNOT_INSPECT_ROI_OOB";
        case OverallReason::CANNOT_INSPECT_LOW_CONFIDENCE: return "CANNOT_INSPECT_LOW_CONFIDENCE";
    }
    return "CANNOT_INSPECT_ORIENTATION";
}

ScrewStatus string_to_screw_status(const std::string& str) {
    if (str == "PRESENT") return ScrewStatus::PRESENT;
    if (str == "MISSING") return ScrewStatus::MISSING;
    if (str == "UNCERTAIN") return ScrewStatus::UNCERTAIN;
    return ScrewStatus::CANNOT_INSPECT;
}

BlockStatus string_to_block_status(const std::string& str) {
    if (str == "PASS") return BlockStatus::PASS;
    if (str == "DEFECT") return BlockStatus::DEFECT;
    return BlockStatus::CANNOT_INSPECT;
}

BlockReason string_to_block_reason(const std::string& str) {
    if (str == "OK") return BlockReason::OK;
    if (str == "MISSING_SCREW") return BlockReason::MISSING_SCREW;
    if (str == "CANNOT_INSPECT_ORIENTATION") return BlockReason::CANNOT_INSPECT_ORIENTATION;
    if (str == "CANNOT_INSPECT_ROI_OOB") return BlockReason::CANNOT_INSPECT_ROI_OOB;
    if (str == "CANNOT_INSPECT_LOW_CONFIDENCE") return BlockReason::CANNOT_INSPECT_LOW_CONFIDENCE;
    if (str == "UNINSPECTED_OVER_CAPACITY") return BlockReason::UNINSPECTED_OVER_CAPACITY;
    return BlockReason::CANNOT_INSPECT_ORIENTATION;
}

OrientationStatus string_to_orientation(const std::string& str) {
    if (str == "0_DEG_CONFIRMED") return OrientationStatus::CONFIRMED_0_DEG;
    if (str == "180_DEG_CONFIRMED") return OrientationStatus::CONFIRMED_180_DEG;
    return OrientationStatus::ORIENTATION_AMBIGUOUS;
}

OverallStatus string_to_overall_status(const std::string& str) {
    if (str == "PASS") return OverallStatus::PASS;
    if (str == "DEFECT") return OverallStatus::DEFECT;
    return OverallStatus::CANNOT_INSPECT;
}

OverallReason string_to_overall_reason(const std::string& str) {
    if (str == "OK") return OverallReason::OK;
    if (str == "DEFECT_UNDER_COUNT") return OverallReason::DEFECT_UNDER_COUNT;
    if (str == "DEFECT_OVER_COUNT") return OverallReason::DEFECT_OVER_COUNT;
    if (str == "DEFECT_MISSING_SCREW") return OverallReason::DEFECT_MISSING_SCREW;
    if (str == "CANNOT_INSPECT_ORIENTATION") return OverallReason::CANNOT_INSPECT_ORIENTATION;
    if (str == "CANNOT_INSPECT_ROI_OOB") return OverallReason::CANNOT_INSPECT_ROI_OOB;
    if (str == "CANNOT_INSPECT_LOW_CONFIDENCE") return OverallReason::CANNOT_INSPECT_LOW_CONFIDENCE;
    return OverallReason::CANNOT_INSPECT_ORIENTATION;
}

std::string InspectionResult::to_json(int indent) const {
    json root = json::object();
    root["schema_version"] = schema_version;
    root["device_id"] = device_id;
    root["app_version"] = app_version;
    root["applied_revision"] = applied_revision;
    root["cycle_id"] = cycle_id;
    root["frame_id"] = frame_id;
    root["timestamp"] = timestamp;
    root["overall_status"] = overall_status_to_string(overall_status);
    root["overall_reason"] = overall_reason_to_string(overall_reason);

    json summary_obj = json::object();
    summary_obj["expected_blocks"] = summary.expected_blocks;
    summary_obj["actual_blocks"] = summary.actual_blocks;
    summary_obj["passed_blocks"] = summary.passed_blocks;
    summary_obj["defect_blocks"] = summary.defect_blocks;
    summary_obj["cannot_inspect_blocks"] = summary.cannot_inspect_blocks;
    root["summary"] = summary_obj;

    json blocks_arr = json::array();
    for (const auto& blk : blocks) {
        json b_obj = json::object();
        b_obj["block_id"] = blk.block_id;
        b_obj["bbox_xywh"] = blk.bbox_xywh;
        b_obj["angle_deg"] = blk.angle_deg;
        b_obj["orientation"] = orientation_to_string(blk.orientation);
        b_obj["status"] = block_status_to_string(blk.status);
        b_obj["reason"] = block_reason_to_string(blk.reason);
        b_obj["expected_screws"] = blk.expected_screws;
        b_obj["actual_screws"] = blk.actual_screws;

        json screws_arr = json::array();
        for (const auto& sc : blk.screws) {
            json s_obj = json::object();
            s_obj["id"] = sc.id;
            s_obj["name"] = sc.name;
            s_obj["status"] = screw_status_to_string(sc.status);
            s_obj["score"] = sc.score;
            s_obj["roi_center_xy"] = {sc.roi_center_xy.first, sc.roi_center_xy.second};
            screws_arr.push_back(s_obj);
        }
        b_obj["screws"] = screws_arr;
        blocks_arr.push_back(b_obj);
    }
    root["blocks"] = blocks_arr;

    if (indent >= 0) {
        return root.dump(indent);
    }
    return root.dump();
}

} // namespace screw_guard
