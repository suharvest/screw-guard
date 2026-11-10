#pragma once

#include "app_config.h"
#include "result_payload.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>
#include <string>

namespace screw_guard {

struct OrientationDecision {
    OrientationStatus orientation = OrientationStatus::ORIENTATION_AMBIGUOUS;
    double s0 = 0.0;
    double s180 = 0.0;
    bool is_valid = false;
};

class ScrewInspector {
public:
    explicit ScrewInspector(const AppConfig& config);
    ~ScrewInspector() = default;

    // Update configuration at runtime
    bool update_config(const AppConfig& new_config, std::string& err_msg);
    const AppConfig& get_config() const { return config_; }

    // Execute full inspection cycle on input image
    InspectionResult inspect(const cv::Mat& image,
                             int64_t frame_id = 0,
                             const std::string& cycle_id = "cyc_001",
                             double timestamp = 0.0);

    // Coarse detection of candidate blocks via 640x360 downsampling and HSV/contour analysis
    std::vector<cv::RotatedRect> coarse_detect_blocks(const cv::Mat& orig_img);

    // 180-degree orientation disambiguation using asymmetric anchor
    // Pre-condition: must strictly verify std::isfinite(S0) && std::isfinite(S180).
    // If not satisfied or ambiguous, outputs ORIENTATION_AMBIGUOUS.
    OrientationDecision disambiguate_orientation(const cv::Mat& orig_gray,
                                                  const cv::RotatedRect& rect,
                                                  cv::Mat& out_M,
                                                  cv::Mat& out_M_inv,
                                                  float& out_angle_deg);

    // Evaluate single screw ROI on original image patch
    // Checks ValidPatch防御: (X_orig - 24 >= 0) && (X_orig + 24 < W) && (Y_orig - 24 >= 0) && (Y_orig + 24 < H)
    // If false: outputs status=CANNOT_INSPECT, score=-1.0.
    ScrewResult evaluate_screw(const cv::Mat& orig_gray,
                               const ScrewRoiConfig& roi_config,
                               const cv::Mat& M_inv,
                               int orig_w,
                               int orig_h,
                               double confidence_threshold);

    // Sobel gradient mean + specular reflection ratio scoring operator on 48x48 patch
    static double compute_screw_score(const cv::Mat& patch_gray,
                                      double& out_edge_mean,
                                      double& out_bright_ratio);

private:
    AppConfig config_;
};

} // namespace screw_guard
