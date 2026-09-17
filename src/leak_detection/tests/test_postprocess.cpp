#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include <opencv2/core.hpp>

#include "leak_detector.h"
#include "postprocess.h"

// 用合成数据验证 PostprocessYoloSeg 的核心行为：
//   1) 空指针不崩溃
//   2) 置信度过滤生效
//   3) NMS 生效
//   4) letterbox 反变换正确
//   5) 掩码裁剪到框内：框外必须全为 0，面积等于框面积
int main() {
  const int kCandidates = 10;
  const int kDims = 37;  // 4 框 + 1 类 + 32 mask 系数
  const int kMasks = 32;
  const int kProto = 320;

  std::vector<float> output0(static_cast<std::size_t>(kDims) * kCandidates, 0.f);
  std::vector<float> output1(static_cast<std::size_t>(kMasks) * kProto * kProto, 0.f);
  const int64_t shape0[3] = {1, kDims, kCandidates};
  const int64_t shape1[4] = {1, kMasks, kProto, kProto};

  // 原型第 0 平面整片置 1，配合系数 10 使 sigmoid(10) ~= 1。
  // 裁剪版下只有框内像素会被点亮，面积应恰好等于框面积。
  for (int i = 0; i < kProto * kProto; ++i) {
    output1[static_cast<std::size_t>(i)] = 1.f;
  }

  auto set = [&](int d, int i, float v) {
    output0[static_cast<std::size_t>(d) * kCandidates + i] = v;
  };

  // 候选 0：正常目标，框 = (540, 590, 200, 100)
  set(0, 0, 640.f); set(1, 0, 640.f); set(2, 0, 200.f); set(3, 0, 100.f); set(4, 0, 0.90f);
  set(5, 0, 10.f);
  // 候选 1：与候选 0 完全重叠，应被 NMS 抑制
  set(0, 1, 640.f); set(1, 1, 640.f); set(2, 1, 200.f); set(3, 1, 100.f); set(4, 1, 0.50f);
  set(5, 1, 10.f);
  // 候选 2..9：置信度低于阈值，应被过滤
  for (int i = 2; i < kCandidates; ++i) {
    set(0, i, 100.f); set(1, i, 100.f); set(2, i, 50.f); set(3, i, 50.f); set(4, i, 0.10f);
    set(5, i, 10.f);
  }

  const cv::Size orig(1280, 1280);

  // 1) 空指针不应崩溃
  const std::vector<leak::LeakItem> null_case =
      leak::PostprocessYoloSeg(nullptr, shape0, nullptr, shape1, orig, 1.f, 0, 0, 0.25f, 0.5f, 0.5f);
  bool ok = null_case.empty();

  const std::vector<leak::LeakItem> items =
      leak::PostprocessYoloSeg(output0.data(), shape0, output1.data(), shape1,
                               orig, 1.f, 0, 0, 0.25f, 0.5f, 0.5f);

  std::cout << "detections = " << items.size() << "\n";
  for (const auto& it : items) {
    std::cout << "  conf=" << it.confidence
              << " box=[" << it.box.x << "," << it.box.y << ","
              << it.box.width << "," << it.box.height << "]"
              << " area=" << it.area
              << " mask=" << it.mask.cols << "x" << it.mask.rows
              << " type=" << it.mask.type() << "\n";
  }

  // 2) 只剩 1 个检测（1 个被 NMS 抑制，8 个被阈值过滤）
  ok = ok && (items.size() == 1);
  if (ok) {
    const cv::Rect box(540, 590, 200, 100);

    // 5) 框外必须全为 0：把框区域清零后再统计非零像素，应为 0
    cv::Mat outside = items[0].mask.clone();
    outside(box).setTo(0);
    const int leaked = cv::countNonZero(outside);

    ok = std::abs(items[0].box.x - 540.f) < 1.f &&
         std::abs(items[0].box.y - 590.f) < 1.f &&
         std::abs(items[0].box.width - 200.f) < 1.f &&
         std::abs(items[0].box.height - 100.f) < 1.f &&
         items[0].mask.type() == CV_8UC1 &&
         items[0].mask.size() == orig &&
         items[0].area == 200 * 100 &&
         leaked == 0;

    std::cout << "area=" << items[0].area << " expected=20000"
              << " leaked_outside_box=" << leaked << "\n";
  }

  std::cout << (ok ? "test_postprocess passed" : "test_postprocess FAILED") << std::endl;
  return ok ? 0 : 1;
}
