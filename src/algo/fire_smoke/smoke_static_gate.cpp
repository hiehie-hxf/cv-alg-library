#include "algo/fire_smoke/smoke_static_gate.h"
#include "base/status.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <opencv2/imgproc.hpp>

// 使用 160x90 降采样灰度图维护跨帧证据，控制实时场景中的差分开销。
namespace cvsdk {
namespace {
constexpr int W = 160, H = 90, BLOCK = 10;
cv::Mat DilateBool(const cv::Mat& mask, int radius) {
  cv::Mat out;
  cv::dilate(mask, out,
             cv::getStructuringElement(cv::MORPH_RECT, cv::Size(radius * 2 + 1, radius * 2 + 1)));
  return out;
}
double Mono() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
float Mean(const cv::Mat& m) {
  return static_cast<float>(cv::mean(m)[0]);
}
} // namespace
cv::Mat SmokeStaticGate::ToSmallGray(const CVSDK_Image& image) const {
  cv::Mat in(static_cast<int>(image.height), static_cast<int>(image.width), CV_8UC3,
             const_cast<uint8_t*>(image.data), image.stride_bytes),
      bgr = in;
  if (image.pixel_format == CVSDK_PIXEL_FORMAT_RGB8)
    cv::cvtColor(in, bgr, cv::COLOR_RGB2BGR);
  cv::Mat small, gray;
  cv::resize(bgr, small, cv::Size(W, H), 0, 0, cv::INTER_AREA);
  cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
  gray.convertTo(gray, CV_32F);
  return gray;
}
cv::Mat SmokeStaticGate::Region(const cv::Mat& m, const CVSDK_Detection& d, int w, int h) const {
  int x1 = std::clamp(static_cast<int>(d.x / w * W), 0, W - 2),
      y1 = std::clamp(static_cast<int>(d.y / h * H), 0, H - 2),
      x2 = std::clamp(static_cast<int>(std::ceil((d.x + d.width) / w * W)), x1 + 2, W),
      y2 = std::clamp(static_cast<int>(std::ceil((d.y + d.height) / h * H)), y1 + 2, H);
  return m(cv::Rect(x1, y1, x2 - x1, y2 - y1));
}
void SmokeStaticGate::Reset() {
  history_.clear();
  last_gray_.release();
  stats_.clear();
}
CVSDK_Status SmokeStaticGate::Filter(const CVSDK_Image& image,
                                     std::vector<CVSDK_Detection>* detections) {
  if (!detections || !config_.smoke_static_gate_enabled)
    return CVSDK_OK;
  const double now = Mono();
  cv::Mat gray = ToSmallGray(image), short_diff;
  if (!last_gray_.empty())
    cv::absdiff(gray, last_gray_, short_diff);
  last_gray_ = gray.clone();
  cv::Mat ref;
  for (auto& p : history_)
    if (now - p.first >= 1.6)
      ref = p.second;
  history_.push_back({now, gray.clone()});
  while (!history_.empty() && now - history_.front().first > 4.0)
    history_.pop_front();
  bool has_smoke = false;
  for (auto& d : *detections)
    has_smoke |= d.class_id == 0;
  if (!has_smoke)
    return CVSDK_OK;
  cv::Mat long_diff;
  if (!ref.empty())
    cv::absdiff(gray, ref, long_diff);
  double base = 0;
  if (!long_diff.empty()) {
    cv::Mat blocks;
    cv::resize(long_diff, blocks, cv::Size(W / BLOCK, H / BLOCK), 0, 0, cv::INTER_AREA);
    base = Mean(blocks);
  }
  const double static_limit =
      std::max<double>(config_.smoke_static_diff, base * config_.smoke_static_ratio);
  cv::Mat soft_map;
  if (!short_diff.empty()) {
    cv::Mat gx, gy, grad, sharp;
    cv::Sobel(gray, gx, CV_32F, 1, 0);
    cv::Sobel(gray, gy, CV_32F, 0, 1);
    cv::magnitude(gx, gy, grad);
    cv::compare(grad, 40, sharp, cv::CMP_GT);
    sharp = DilateBool(sharp, 8);
    cv::compare(short_diff, std::max(4.0, base * 3.0), soft_map, cv::CMP_GT);
    soft_map.setTo(0, sharp);
  }
  std::vector<CVSDK_Detection> kept;
  for (auto& d : *detections) {
    if (d.class_id != 0) {
      kept.push_back(d);
      continue;
    }
    if (d.score >= config_.smoke_bypass_conf) {
      stats_["bypass"]++;
      kept.push_back(d);
      continue;
    }
    cv::Mat r = Region(gray, d, image.width, image.height);
    if (Mean(r >= 245) >= config_.smoke_overexposed_max) {
      stats_["overexposed"]++;
      continue;
    }
    double core = Mean(r >= 250);
    if (Mean(r) >= config_.smoke_halo_mean ||
        (core >= config_.smoke_halo_core_frac && Mean(r) >= config_.smoke_halo_core_mean)) {
      stats_["halo"]++;
      continue;
    }
    if (long_diff.empty() || soft_map.empty()) {
      stats_["warmup"]++;
      continue;
    }
    cv::Mat lr = Region(long_diff, d, image.width, image.height);
    double lm = Mean(lr);
    int active = cv::countNonZero(lr > std::max(6.0, base * 4.0));
    if (lm <= static_limit && active < 25) {
      stats_["static"]++;
      continue;
    }
    int soft = cv::countNonZero(Region(soft_map, d, image.width, image.height));
    if (soft < static_cast<int>(config_.smoke_soft_active_min)) {
      stats_["no_soft_motion"]++;
      continue;
    }
    stats_["kept"]++;
    kept.push_back(d);
  }
  *detections = std::move(kept);
  return CVSDK_OK;
}
} // namespace cvsdk
