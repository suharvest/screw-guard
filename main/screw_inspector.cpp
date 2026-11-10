#include "screw_inspector.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace screw_guard {

ScrewInspector::ScrewInspector(const AppConfig& config) {
    std::string err_code, err_msg;
    AppConfig validated;
    std::string json_to_check = config.profile_json_raw;
    if (json_to_check.empty()) {
        nlohmann::json j;
        j["revision"] = config.profile.revision;
        j["part_name"] = config.profile.part_name;
        j["expected_blocks"] = config.profile.expected_blocks;
        j["expected_screws_per_block"] = config.profile.expected_screws_per_block;
        j["canonical_size"] = { config.profile.canonical_width, config.profile.canonical_height };
        j["min_block_area"] = config.profile.min_block_area;
        j["anchor"] = {
            {"norm_x", config.profile.anchor.norm_x},
            {"norm_y", config.profile.anchor.norm_y},
            {"norm_w", config.profile.anchor.norm_w},
            {"norm_h", config.profile.anchor.norm_h},
            {"ref_edge_mean", config.profile.anchor.ref_edge_mean}
        };
        nlohmann::json rois = nlohmann::json::array();
        for (const auto& r : config.profile.screw_rois) {
            rois.push_back({
                {"id", r.id},
                {"name", r.name},
                {"norm_x", r.norm_x},
                {"norm_y", r.norm_y},
                {"norm_r", r.norm_r}
            });
        }
        j["screw_rois"] = rois;
        json_to_check = j.dump();
    }

    if (!ConfigValidator::parse_app_config(json_to_check, config.confidence_threshold, config.max_blocks, validated, err_code, err_msg)) {
        throw std::invalid_argument("Invalid ScrewInspector configuration: " + err_code + " - " + err_msg);
    }
    config_ = validated;
}

bool ScrewInspector::update_config(const AppConfig& new_config, std::string& err_msg) {
    std::string err_code;
    AppConfig validated;
    std::string json_to_check = new_config.profile_json_raw;
    if (json_to_check.empty()) {
        nlohmann::json j;
        j["revision"] = new_config.profile.revision;
        j["part_name"] = new_config.profile.part_name;
        j["expected_blocks"] = new_config.profile.expected_blocks;
        j["expected_screws_per_block"] = new_config.profile.expected_screws_per_block;
        j["canonical_size"] = { new_config.profile.canonical_width, new_config.profile.canonical_height };
        j["min_block_area"] = new_config.profile.min_block_area;
        j["anchor"] = {
            {"norm_x", new_config.profile.anchor.norm_x},
            {"norm_y", new_config.profile.anchor.norm_y},
            {"norm_w", new_config.profile.anchor.norm_w},
            {"norm_h", new_config.profile.anchor.norm_h},
            {"ref_edge_mean", new_config.profile.anchor.ref_edge_mean}
        };
        nlohmann::json rois = nlohmann::json::array();
        for (const auto& r : new_config.profile.screw_rois) {
            rois.push_back({
                {"id", r.id},
                {"name", r.name},
                {"norm_x", r.norm_x},
                {"norm_y", r.norm_y},
                {"norm_r", r.norm_r}
            });
        }
        j["screw_rois"] = rois;
        json_to_check = j.dump();
    }

    if (!ConfigValidator::parse_app_config(json_to_check, new_config.confidence_threshold, new_config.max_blocks, validated, err_code, err_msg)) {
        return false;
    }
    config_ = validated;
    return true;
}

double ScrewInspector::compute_screw_score(const cv::Mat& patch_gray,
                                          double& out_edge_mean,
                                          double& out_bright_ratio) {
    if (patch_gray.empty() || patch_gray.rows != 48 || patch_gray.cols != 48) {
        out_edge_mean = 0.0;
        out_bright_ratio = 0.0;
        return -1.0;
    }

    // 1. Sobel 绝对加权梯度均值 (衡量十字槽/内六角边缘能量)
    cv::Mat grad_x, grad_y, abs_grad;
    cv::Sobel(patch_gray, grad_x, CV_16S, 1, 0, 3);
    cv::Sobel(patch_gray, grad_y, CV_16S, 0, 1, 3);
    cv::convertScaleAbs(grad_x, grad_x);
    cv::convertScaleAbs(grad_y, grad_y);
    cv::addWeighted(grad_x, 0.5, grad_y, 0.5, 0, abs_grad);
    out_edge_mean = cv::mean(abs_grad)[0];

    // 2. 金属镜面反光占比 (同轴光下金属顶部高亮)
    int bright_count = cv::countNonZero(patch_gray > 165);
    out_bright_ratio = static_cast<double>(bright_count) / (patch_gray.rows * patch_gray.cols);

    // 3. 归一化综合得分
    double score = 0.55 * std::min(1.0, out_edge_mean / 28.0) + 0.45 * std::min(1.0, out_bright_ratio / 0.08);
    return score;
}

std::vector<cv::RotatedRect> ScrewInspector::coarse_detect_blocks(const cv::Mat& orig_img) {
    std::vector<cv::RotatedRect> detected;
    if (orig_img.empty() || orig_img.cols < 10 || orig_img.rows < 10) {
        return detected;
    }

    // Downsample to 640x360
    cv::Mat small_img;
    double scale_x = static_cast<double>(orig_img.cols) / 640.0;
    double scale_y = static_cast<double>(orig_img.rows) / 360.0;
    cv::resize(orig_img, small_img, cv::Size(640, 360), 0, 0, cv::INTER_LINEAR);

    // HSV & Contour analysis
    cv::Mat mask;
    if (small_img.channels() == 3) {
        cv::Mat hsv;
        cv::cvtColor(small_img, hsv, cv::COLOR_BGR2HSV);
        cv::inRange(hsv, cv::Scalar(0, 20, 20), cv::Scalar(180, 255, 255), mask);
        // Fallback to Otsu threshold if saturation is low (e.g. grayscale or metallic)
        if (cv::countNonZero(mask) < (small_img.rows * small_img.cols * 0.01)) {
            cv::Mat gray;
            cv::cvtColor(small_img, gray, cv::COLOR_BGR2GRAY);
            cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        }
    } else {
        cv::threshold(small_img, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    }

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double scaled_min_area = config_.profile.min_block_area / (scale_x * scale_y);

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < scaled_min_area) continue;

        // Restore contour coordinates before fitting: anisotropic scaling changes
        // both the angle and the side lengths of a rotated rectangle.
        std::vector<cv::Point2f> original_contour;
        original_contour.reserve(c.size());
        for (const auto& point : c) {
            original_contour.emplace_back(point.x * static_cast<float>(scale_x),
                                          point.y * static_cast<float>(scale_y));
        }
        detected.push_back(cv::minAreaRect(original_contour));
    }

    // Sort detected blocks using strict (x, y) lexicographical order to guarantee Strict Weak Ordering
    std::sort(detected.begin(), detected.end(), [](const cv::RotatedRect& a, const cv::RotatedRect& b) {
        if (a.center.x != b.center.x) {
            return a.center.x < b.center.x;
        }
        return a.center.y < b.center.y;
    });

    return detected;
}

OrientationDecision ScrewInspector::disambiguate_orientation(const cv::Mat& orig_gray,
                                                            const cv::RotatedRect& rect,
                                                            cv::Mat& out_M,
                                                            cv::Mat& out_M_inv,
                                                            float& out_angle_deg) {
    OrientationDecision dec;
    dec.is_valid = false;
    dec.orientation = OrientationStatus::ORIENTATION_AMBIGUOUS;

    // 尺寸退化硬防御: 避免 cv::getAffineTransform 异常
    if (rect.size.width < 5.0f || rect.size.height < 5.0f) {
        out_angle_deg = rect.angle;
        return dec;
    }

    int W_canon = config_.profile.canonical_width;
    int H_canon = config_.profile.canonical_height;

    float bw = rect.size.width;
    float bh = rect.size.height;
    float angle = rect.angle;

    // Align orientation with canonical canvas aspect ratio
    bool box_is_tall = (bw < bh);
    bool canon_is_tall = (W_canon < H_canon);
    if (box_is_tall != canon_is_tall) {
        std::swap(bw, bh);
        angle += 90.0f;
    }

    // Normalize initial pose angle to (-90, 90] so candidate 0 consistently aligns with +X
    while (angle > 90.0f) angle -= 180.0f;
    while (angle <= -90.0f) angle += 180.0f;

    float rad = angle * static_cast<float>(CV_PI) / 180.0f;
    float ux = std::cos(rad);
    float uy = std::sin(rad);
    float vx = -std::sin(rad);
    float vy = std::cos(rad);

    cv::Point2f C = rect.center;
    cv::Point2f Px = C + cv::Point2f(ux * (bw * 0.5f), uy * (bw * 0.5f));
    cv::Point2f Py = C + cv::Point2f(vx * (bh * 0.5f), vy * (bh * 0.5f));

    cv::Point2f src[3] = { C, Px, Py };
    cv::Point2f dst[3] = {
        cv::Point2f(W_canon * 0.5f, H_canon * 0.5f),
        cv::Point2f(static_cast<float>(W_canon), H_canon * 0.5f),
        cv::Point2f(W_canon * 0.5f, static_cast<float>(H_canon))
    };

    cv::Mat M_0 = cv::getAffineTransform(src, dst);
    cv::Mat M_0_inv;
    cv::invertAffineTransform(M_0, M_0_inv);

    // Warp to canonical canvas
    cv::Mat canon_0;
    cv::warpAffine(orig_gray, canon_0, M_0, cv::Size(W_canon, H_canon), cv::INTER_LINEAR);

    cv::Mat canon_180;
    cv::rotate(canon_0, canon_180, cv::ROTATE_180);

    // Anchor ROI (norm_x, norm_y are center coordinates)
    int wa = static_cast<int>(std::round(config_.profile.anchor.norm_w * W_canon));
    int ha = static_cast<int>(std::round(config_.profile.anchor.norm_h * H_canon));
    int xa = static_cast<int>(std::round(config_.profile.anchor.norm_x * (W_canon - 1) - wa * 0.5));
    int ya = static_cast<int>(std::round(config_.profile.anchor.norm_y * (H_canon - 1) - ha * 0.5));

    if (wa <= 0 || ha <= 0 || xa < 0 || ya < 0 ||
        xa + wa > W_canon || ya + ha > H_canon) {
        out_M = M_0;
        out_M_inv = M_0_inv;
        out_angle_deg = angle;
        return dec;
    }

    cv::Rect anchor_rect(xa, ya, wa, ha);
    cv::Mat roi_0 = canon_0(anchor_rect);
    cv::Mat roi_180 = canon_180(anchor_rect);

    auto compute_anchor_score = [](const cv::Mat& roi, double ref_edge_mean) -> double {
        if (roi.empty() || roi.rows < 2 || roi.cols < 2) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        cv::Mat gx, gy, abs_g;
        cv::Sobel(roi, gx, CV_16S, 1, 0, 3);
        cv::Sobel(roi, gy, CV_16S, 0, 1, 3);
        cv::convertScaleAbs(gx, gx);
        cv::convertScaleAbs(gy, gy);
        cv::addWeighted(gx, 0.5, gy, 0.5, 0, abs_g);
        double em = cv::mean(abs_g)[0];
        if (!std::isfinite(em)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        double score = std::min(1.0, em / std::max(ref_edge_mean, 1.0));
        return std::clamp(score, 0.0, 1.0);
    };

    double S0 = compute_anchor_score(roi_0, config_.profile.anchor.ref_edge_mean);
    double S180 = compute_anchor_score(roi_180, config_.profile.anchor.ref_edge_mean);

    dec.s0 = S0;
    dec.s180 = S180;

    // 前置硬防御：必须严格满足 std::isfinite(S0) && std::isfinite(S180)
    // 若任一候选为 NaN、Inf 或 ROI 提取失败，立刻判定为朝向歧义，严禁将异常值置零后继续参与比较！
    if (!std::isfinite(S0) || !std::isfinite(S180)) {
        dec.orientation = OrientationStatus::ORIENTATION_AMBIGUOUS;
        dec.is_valid = false;
        out_M = M_0;
        out_M_inv = M_0_inv;
        out_angle_deg = angle;
        return dec;
    }

    // 互斥决策分支
    if (S0 >= 0.65 && (S0 - S180 >= 0.25)) {
        // 分支 1（正向确定）
        dec.orientation = OrientationStatus::CONFIRMED_0_DEG;
        dec.is_valid = true;
        out_M = M_0;
        out_M_inv = M_0_inv;
        float norm_angle = angle;
        while (norm_angle > 180.0f) norm_angle -= 360.0f;
        while (norm_angle <= -180.0f) norm_angle += 360.0f;
        out_angle_deg = norm_angle;
    } else if (S180 >= 0.65 && (S180 - S0 >= 0.25)) {
        // 分支 2（倒向确定）
        dec.orientation = OrientationStatus::CONFIRMED_180_DEG;
        dec.is_valid = true;

        cv::Mat R180 = (cv::Mat_<double>(3, 3) <<
            -1.0,  0.0, static_cast<double>(W_canon - 1),
             0.0, -1.0, static_cast<double>(H_canon - 1),
             0.0,  0.0, 1.0);
        cv::Mat M0_3x3 = cv::Mat::eye(3, 3, CV_64F);
        M_0.copyTo(M0_3x3(cv::Rect(0, 0, 3, 2)));
        cv::Mat M180_3x3 = R180 * M0_3x3;
        out_M = M180_3x3(cv::Rect(0, 0, 3, 2)).clone();
        cv::invertAffineTransform(out_M, out_M_inv);

        float norm_angle = angle + 180.0f;
        while (norm_angle > 180.0f) norm_angle -= 360.0f;
        while (norm_angle <= -180.0f) norm_angle += 360.0f;
        out_angle_deg = norm_angle;
    } else {
        // 分支 3（其余所有情况穷尽归一）
        dec.orientation = OrientationStatus::ORIENTATION_AMBIGUOUS;
        dec.is_valid = false;
        out_M = M_0;
        out_M_inv = M_0_inv;
        float norm_angle = angle;
        while (norm_angle > 180.0f) norm_angle -= 360.0f;
        while (norm_angle <= -180.0f) norm_angle += 360.0f;
        out_angle_deg = norm_angle;
    }

    return dec;
}

ScrewResult ScrewInspector::evaluate_screw(const cv::Mat& orig_gray,
                                         const ScrewRoiConfig& roi_config,
                                         const cv::Mat& M_inv,
                                         int orig_w,
                                         int orig_h,
                                         double confidence_threshold) {
    ScrewResult res;
    res.id = roi_config.id;
    res.name = roi_config.name;

    int W_canon = config_.profile.canonical_width;
    int H_canon = config_.profile.canonical_height;

    int xc = static_cast<int>(std::round(roi_config.norm_x * (W_canon - 1)));
    int yc = static_cast<int>(std::round(roi_config.norm_y * (H_canon - 1)));

    // Map (xc, yc) via M_inv to (X_orig, Y_orig)
    double X_orig = M_inv.at<double>(0, 0) * xc + M_inv.at<double>(0, 1) * yc + M_inv.at<double>(0, 2);
    double Y_orig = M_inv.at<double>(1, 0) * xc + M_inv.at<double>(1, 1) * yc + M_inv.at<double>(1, 2);

    res.roi_center_xy = { std::round(X_orig * 10.0) / 10.0, std::round(Y_orig * 10.0) / 10.0 };

    // ValidPatch 边界防御: (X_orig - 24 >= 0) && (X_orig + 24 < W_orig) && (Y_orig - 24 >= 0) && (Y_orig + 24 < H_orig)
    bool valid_patch = (X_orig - 24.0 >= 0.0) &&
                       (X_orig + 24.0 < orig_w) &&
                       (Y_orig - 24.0 >= 0.0) &&
                       (Y_orig + 24.0 < orig_h);

    if (!valid_patch) {
        // 若 ValidPatch 为假: 该孔位状态严格标记为 CANNOT_INSPECT，score 输出 -1.0
        res.status = ScrewStatus::CANNOT_INSPECT;
        res.score = -1.0;
        return res;
    }

    int px = static_cast<int>(std::round(X_orig - 24.0));
    int py = static_cast<int>(std::round(Y_orig - 24.0));
    px = std::clamp(px, 0, orig_w - 48);
    py = std::clamp(py, 0, orig_h - 48);

    cv::Rect patch_roi(px, py, 48, 48);
    cv::Mat patch = orig_gray(patch_roi);

    double edge_mean = 0.0, bright_ratio = 0.0;
    double score = compute_screw_score(patch, edge_mean, bright_ratio);

    if (!std::isfinite(score)) {
        res.status = ScrewStatus::CANNOT_INSPECT;
        res.score = -1.0;
    } else {
        score = std::clamp(score, 0.0, 1.0);
        // 先做两位小数四舍五入截断，再执行三态互斥区间判定，避免 0.598 被判为 UNCERTAIN 却输出 0.60
        score = std::round(score * 100.0) / 100.0;
        res.score = score;
        if (score >= confidence_threshold) {
            res.status = ScrewStatus::PRESENT;
        } else if (score < 0.35) {
            res.status = ScrewStatus::MISSING;
        } else {
            // 0.35 <= score < confidence_threshold
            res.status = ScrewStatus::UNCERTAIN;
        }
    }

    return res;
}

InspectionResult ScrewInspector::inspect(const cv::Mat& image,
                                        int64_t frame_id,
                                        const std::string& cycle_id,
                                        double timestamp) {
    InspectionResult res;
    res.schema_version = "2.4";
    res.device_id = "recamera-sg2002-station01";
    res.app_version = "1.0.0";
    res.applied_revision = config_.profile.revision;
    res.cycle_id = cycle_id;
    res.frame_id = frame_id;
    res.timestamp = timestamp;

    cv::Mat orig_gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, orig_gray, cv::COLOR_BGR2GRAY);
    } else {
        orig_gray = image.clone();
    }

    // 1. Coarse detection
    std::vector<cv::RotatedRect> detected_rects = coarse_detect_blocks(image);
    int actual_blocks = static_cast<int>(detected_rects.size());

    res.summary.expected_blocks = config_.profile.expected_blocks;
    res.summary.actual_blocks = actual_blocks;

    int passed_count = 0;
    int defect_count = 0;
    int cannot_inspect_count = 0;

    int inspect_limit = std::min(actual_blocks, config_.max_blocks);

    for (int i = 0; i < inspect_limit; ++i) {
        const auto& rect = detected_rects[i];
        BlockResult block;
        block.block_id = i + 1;
        cv::Rect bbox = rect.boundingRect();
        bbox &= cv::Rect(0, 0, image.cols, image.rows);
        block.bbox_xywh = {
            static_cast<double>(bbox.x),
            static_cast<double>(bbox.y),
            static_cast<double>(bbox.width),
            static_cast<double>(bbox.height)
        };
        block.expected_screws = config_.profile.expected_screws_per_block;

        cv::Mat M, M_inv;
        float angle_deg = 0.0f;
        OrientationDecision ori_dec = disambiguate_orientation(orig_gray, rect, M, M_inv, angle_deg);
        block.angle_deg = std::round(angle_deg * 10.0f) / 10.0f;
        block.orientation = ori_dec.orientation;

        if (!ori_dec.is_valid) {
            // 朝向歧义: 直接输出 CANNOT_INSPECT_ORIENTATION, 严禁使用螺丝存在性反推朝向
            block.status = BlockStatus::CANNOT_INSPECT;
            block.reason = BlockReason::CANNOT_INSPECT_ORIENTATION;
            block.actual_screws = 0;
            block.screws = {};
            cannot_inspect_count++;
            res.blocks.push_back(block);
            continue;
        }

        // 精检逐孔
        int present_count = 0;
        bool has_missing = false;
        bool has_oob = false;
        bool has_uncertain = false;

        for (const auto& roi_cfg : config_.profile.screw_rois) {
            ScrewResult sc = evaluate_screw(orig_gray, roi_cfg, M_inv, image.cols, image.rows, config_.confidence_threshold);
            if (sc.status == ScrewStatus::PRESENT) {
                present_count++;
            } else if (sc.status == ScrewStatus::MISSING) {
                has_missing = true;
            } else if (sc.status == ScrewStatus::CANNOT_INSPECT) {
                has_oob = true;
            } else if (sc.status == ScrewStatus::UNCERTAIN) {
                has_uncertain = true;
            }
            block.screws.push_back(sc);
        }

        block.actual_screws = present_count;

        // 孔级到工件级的状态聚合优先级 (SPEC §4.2)
        // 1. DEFECT: 任一孔为 MISSING -> MISSING_SCREW
        // 2. CANNOT_INSPECT (ROI_OOB): 无 MISSING，但有 CANNOT_INSPECT -> CANNOT_INSPECT_ROI_OOB
        // 3. CANNOT_INSPECT (LOW_CONFIDENCE): 无 MISSING/CANNOT_INSPECT，但有 UNCERTAIN -> CANNOT_INSPECT_LOW_CONFIDENCE
        // 4. PASS: 全部为 PRESENT -> OK
        if (has_missing) {
            block.status = BlockStatus::DEFECT;
            block.reason = BlockReason::MISSING_SCREW;
            defect_count++;
        } else if (has_oob) {
            block.status = BlockStatus::CANNOT_INSPECT;
            block.reason = BlockReason::CANNOT_INSPECT_ROI_OOB;
            cannot_inspect_count++;
        } else if (has_uncertain) {
            block.status = BlockStatus::CANNOT_INSPECT;
            block.reason = BlockReason::CANNOT_INSPECT_LOW_CONFIDENCE;
            cannot_inspect_count++;
        } else {
            block.status = BlockStatus::PASS;
            block.reason = BlockReason::OK;
            passed_count++;
        }

        res.blocks.push_back(block);
    }

    // 超量工件处理 (SPEC §4.3 item 2)
    // 第 max_blocks + 1 及后续工件约定:
    // status: CANNOT_INSPECT, reason: UNINSPECTED_OVER_CAPACITY, orientation: ORIENTATION_AMBIGUOUS,
    // expected_screws: expected_screws_per_block, actual_screws: 0, screws: []
    for (int i = inspect_limit; i < actual_blocks; ++i) {
        const auto& rect = detected_rects[i];
        BlockResult block;
        block.block_id = i + 1;
        cv::Rect bbox = rect.boundingRect();
        bbox &= cv::Rect(0, 0, image.cols, image.rows);
        block.bbox_xywh = {
            static_cast<double>(bbox.x),
            static_cast<double>(bbox.y),
            static_cast<double>(bbox.width),
            static_cast<double>(bbox.height)
        };
        block.angle_deg = std::round(rect.angle * 10.0f) / 10.0f;
        block.orientation = OrientationStatus::ORIENTATION_AMBIGUOUS;
        block.status = BlockStatus::CANNOT_INSPECT;
        block.reason = BlockReason::UNINSPECTED_OVER_CAPACITY;
        block.expected_screws = config_.profile.expected_screws_per_block;
        block.actual_screws = 0;
        block.screws = {};

        cannot_inspect_count++;
        res.blocks.push_back(block);
    }

    res.summary.passed_blocks = passed_count;
    res.summary.defect_blocks = defect_count;
    res.summary.cannot_inspect_blocks = cannot_inspect_count;

    // 整批仲裁优先级与主原因选择规则 (SPEC §4.3 item 3)
    // DEFECT > CANNOT_INSPECT > PASS
    // DEFECT_UNDER_COUNT > DEFECT_OVER_COUNT > DEFECT_MISSING_SCREW >
    // CANNOT_INSPECT_ORIENTATION > CANNOT_INSPECT_ROI_OOB > CANNOT_INSPECT_LOW_CONFIDENCE > OK
    if (actual_blocks < config_.profile.expected_blocks) {
        res.overall_status = OverallStatus::DEFECT;
        res.overall_reason = OverallReason::DEFECT_UNDER_COUNT;
    } else if (actual_blocks > config_.profile.expected_blocks) {
        res.overall_status = OverallStatus::DEFECT;
        res.overall_reason = OverallReason::DEFECT_OVER_COUNT;
    } else {
        // actual_blocks == expected_blocks
        if (defect_count > 0) {
            res.overall_status = OverallStatus::DEFECT;
            res.overall_reason = OverallReason::DEFECT_MISSING_SCREW;
        } else if (cannot_inspect_count > 0) {
            res.overall_status = OverallStatus::CANNOT_INSPECT;
            bool has_orientation_issue = false;
            bool has_oob_issue = false;
            bool has_low_conf_issue = false;

            for (const auto& b : res.blocks) {
                if (b.reason == BlockReason::CANNOT_INSPECT_ORIENTATION) {
                    has_orientation_issue = true;
                } else if (b.reason == BlockReason::CANNOT_INSPECT_ROI_OOB) {
                    has_oob_issue = true;
                } else if (b.reason == BlockReason::CANNOT_INSPECT_LOW_CONFIDENCE) {
                    has_low_conf_issue = true;
                }
            }

            if (has_orientation_issue) {
                res.overall_reason = OverallReason::CANNOT_INSPECT_ORIENTATION;
            } else if (has_oob_issue) {
                res.overall_reason = OverallReason::CANNOT_INSPECT_ROI_OOB;
            } else if (has_low_conf_issue) {
                res.overall_reason = OverallReason::CANNOT_INSPECT_LOW_CONFIDENCE;
            } else {
                res.overall_reason = OverallReason::CANNOT_INSPECT_LOW_CONFIDENCE;
            }
        } else {
            res.overall_status = OverallStatus::PASS;
            res.overall_reason = OverallReason::OK;
        }
    }

    return res;
}

} // namespace screw_guard
