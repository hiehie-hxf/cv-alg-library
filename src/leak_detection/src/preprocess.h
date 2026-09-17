#pragma once

#include <opencv2/core.hpp>

namespace leak {

struct LetterboxResult {
  cv::Mat image;
  float ratio = 1.f;
  int pad_x = 0;
  int pad_y = 0;
};

// 等比缩放并填充到目标尺寸，padding 值为 114。
LetterboxResult Letterbox(const cv::Mat& src, int target_w, int target_h);

}  // namespace leak
