#include "algo/fire_smoke/demo_torch_gate.h"
#include <opencv2/imgproc.hpp>
namespace cvsdk {
std::vector<CVSDK_Detection> DetectDemoTorch(const CVSDK_Image& image, const FireConfig& c) {
  std::vector<CVSDK_Detection> out;
  if (!c.demo_torch_enabled || !image.data || image.pixel_format == CVSDK_PIXEL_FORMAT_GRAY8)
    return out;
  cv::Mat in(static_cast<int>(image.height), static_cast<int>(image.width), CV_8UC3,
             const_cast<uint8_t*>(image.data), image.stride_bytes),
      bgr = in;
  if (image.pixel_format == CVSDK_PIXEL_FORMAT_RGB8)
    cv::cvtColor(in, bgr, cv::COLOR_RGB2BGR);
  cv::Mat hsv;
  cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
  std::vector<cv::Mat> ch;
  cv::split(hsv, ch);
  cv::Mat vivid = (ch[1] >= 140) & (ch[2] >= 100), red = vivid & ((ch[0] <= 12) | (ch[0] >= 170)),
          yellow = vivid & (ch[0] >= 18) & (ch[0] <= 42), mask = (red | yellow);
  cv::morphologyEx(mask, mask, cv::MORPH_CLOSE,
                   cv::getStructuringElement(cv::MORPH_ELLIPSE, {9, 9}));
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
  float area = static_cast<float>(image.width) * image.height;
  for (auto& contour : contours) {
    auto r = cv::boundingRect(contour);
    double ar = cv::contourArea(contour) / area,
           aspect = std::max(r.height / (double)std::max(1, r.width),
                             r.width / (double)std::max(1, r.height));
    if (ar < c.demo_torch_min_area_ratio || aspect < c.demo_torch_min_aspect)
      continue;
    out.push_back(
        {(float)r.x, (float)r.y, (float)r.width, (float)r.height, c.demo_torch_confidence, 1});
  }
  return out;
}
} // namespace cvsdk
