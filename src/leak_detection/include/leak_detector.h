#pragma once

#include <string>
#include <vector>
#include <memory>
#include <opencv2/core.hpp>

namespace leak {

struct LeakItem {
    float confidence = 0.f;
    cv::Rect2f box;
    cv::Mat mask;
    int area = 0;
};

struct LeakAlertState {
    bool alert = false;
    int hit_count = 0;
    bool area_growing = false;
    bool centroid_down = false;
};

struct LeakConfig {
    float candidate_conf = 0.25f;
    float iou_threshold = 0.5f;
    float mask_threshold = 0.5f;
    int window = 10;
    int min_hits = 3;
    float area_growth_ratio = 1.15f;
    float centroid_down_px = 5.0f;
    int input_w = 1280;
    int input_h = 1280;
};

class LeakDetector {
public:
    LeakDetector();
    ~LeakDetector();

    LeakDetector(const LeakDetector&) = delete;
    LeakDetector& operator=(const LeakDetector&) = delete;

    bool LoadModel(const std::string& onnx_path);
    bool LoadConfig(const std::string& json_path);

    bool Infer(const cv::Mat& bgr,
               std::vector<LeakItem>* items,
               LeakAlertState* state);

    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace leak