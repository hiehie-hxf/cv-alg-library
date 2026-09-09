#include "algo/fire_smoke/fire_color_gate.h"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace cvsdk {
float FireColorFraction(const CVSDK_Image& image, const CVSDK_Detection& box) {
  if (!image.data ||
      (image.pixel_format != CVSDK_PIXEL_FORMAT_BGR8 &&
       image.pixel_format != CVSDK_PIXEL_FORMAT_RGB8) ||
      box.width <= 1 || box.height <= 1)
    return 0.F;
  const int x0 = std::max(0, static_cast<int>(std::floor(box.x))),
            y0 = std::max(0, static_cast<int>(std::floor(box.y)));
  const int x1 = std::min(static_cast<int>(image.width),
                          static_cast<int>(std::ceil(box.x + box.width))),
            y1 = std::min(static_cast<int>(image.height),
                          static_cast<int>(std::ceil(box.y + box.height)));
  if (x1 - x0 < 2 || y1 - y0 < 2)
    return 0.F;
  cv::Mat input(static_cast<int>(image.height), static_cast<int>(image.width), CV_8UC3,
                const_cast<uint8_t*>(image.data), image.stride_bytes);
  cv::Mat bgr = input;
  if (image.pixel_format == CVSDK_PIXEL_FORMAT_RGB8)
    cv::cvtColor(input, bgr, cv::COLOR_RGB2BGR);
  cv::Rect roi(x0, y0, x1 - x0, y1 - y0);
  cv::Mat crop = bgr(roi);
  const int max_side = 96;
  if (std::max(crop.cols, crop.rows) > max_side) {
    const double scale = static_cast<double>(max_side) / std::max(crop.cols, crop.rows);
    cv::resize(crop, crop, cv::Size(), scale, scale, cv::INTER_AREA);
  }
  cv::Mat hsv;
  cv::cvtColor(crop, hsv, cv::COLOR_BGR2HSV);
  uint64_t warm = 0, total = hsv.total();
  for (int y = 0; y < hsv.rows; ++y)
    for (int x = 0; x < hsv.cols; ++x) {
      const auto p = hsv.at<cv::Vec3b>(y, x);
      if ((p[0] <= 35 || p[0] >= 170) && p[1] >= 130 && p[2] >= 200)
        ++warm;
    }
  return total ? static_cast<float>(warm) / total : 0.F;
}
bool PassFireColorGate(const CVSDK_Image& image, const CVSDK_Detection& box, float min_fraction) {
  return FireColorFraction(image, box) >= min_fraction;
}
} // namespace cvsdk
