#include "preprocess.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace leak {

LetterboxResult Letterbox(const cv::Mat& src, int target_w, int target_h) {
  LetterboxResult result;

  if (src.empty() || target_w <= 0 || target_h <= 0) {
    return result;
  }

  const float ratio = std::min(
      static_cast<float>(target_w) / static_cast<float>(src.cols),
      static_cast<float>(target_h) / static_cast<float>(src.rows));

  const int new_w = static_cast<int>(std::lround(src.cols * ratio));
  const int new_h = static_cast<int>(std::lround(src.rows * ratio));

  cv::Mat resized;
  cv::resize(src, resized, cv::Size(new_w, new_h));

  const int pad_x = (target_w - new_w) / 2;
  const int pad_y = (target_h - new_h) / 2;

  cv::copyMakeBorder(resized, result.image, pad_y, target_h - new_h - pad_y,
                     pad_x, target_w - new_w - pad_x, cv::BORDER_CONSTANT,
                     cv::Scalar(114, 114, 114));

  result.ratio = ratio;
  result.pad_x = pad_x;
  result.pad_y = pad_y;
  return result;
}

}  // namespace leak
