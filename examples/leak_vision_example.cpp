/**
 * @file leak_vision_example.cpp
 * @brief 基于 CV SDK 的漏液分割最小调用示例。
 *
 * 示例只使用公开 C ABI：加载模型包、处理单张图片、取走实例掩码、绘制半透明掩码与检测框，
 * 并保存结果图。不做实时取流和窗口显示，因此可在无图形界面的环境（CI、SSH）中直接运行。
 */

#include "cv_sdk/cv_sdk.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

/**
 * @brief 返回漏液告警等级名称。
 * @param level SDK 告警等级。
 * @return 稳定的英文枚举名称。
 */
const char* AlertLevelName(CVSDK_LeakAlertLevel level) {
  switch (level) {
  case CVSDK_LEAK_ALERT_WARNING:
    return "warning";
  default:
    return "none";
  }
}

/**
 * @brief 取走某个目标的实例掩码。
 * @param processor 已处理过一帧的处理器句柄。
 * @param index 目标下标。
 * @return 独立的 CV_8UC1 掩码副本；失败时返回空 Mat。
 */
cv::Mat FetchMask(CVSDK_LeakProcessor* processor, uint32_t index) {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  // 先查尺寸：buffer 传 NULL 时返回 BUFFER_TOO_SMALL 并写出宽高。
  const CVSDK_Status queried =
      CVSDK_LeakProcessorCopyMask(processor, index, nullptr, 0, &width, &height, &stride);
  if (queried != CVSDK_BUFFER_TOO_SMALL || width == 0 || height == 0)
    return cv::Mat();
  std::vector<uint8_t> buffer(static_cast<size_t>(width) * height);
  const CVSDK_Status copied =
      CVSDK_LeakProcessorCopyMask(processor, index, buffer.data(),
                                  static_cast<uint32_t>(buffer.size()), &width, &height, &stride);
  if (copied != CVSDK_OK)
    return cv::Mat();
  return cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC1, buffer.data()).clone();
}

/**
 * @brief 在图像上叠加半透明掩码，并绘制检测框、质心和告警横幅。
 * @param processor 已处理过一帧的处理器句柄，用于取走掩码。
 * @param items 漏液目标数组。
 * @param state 当前告警状态。
 * @param canvas 待绘制的图像副本。
 * @return 实际叠加了掩码的目标数量。
 */
uint32_t Draw(CVSDK_LeakProcessor* processor, const std::vector<CVSDK_LeakItem>& items,
              const CVSDK_LeakAlertState& state, cv::Mat* canvas) {
  uint32_t overlaid = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    const CVSDK_LeakItem& item = items[i];
    const cv::Mat mask = FetchMask(processor, static_cast<uint32_t>(i));
    if (!mask.empty()) {
      cv::Mat scaled = mask;
      if (mask.size() != canvas->size())
        cv::resize(mask, scaled, canvas->size(), 0, 0, cv::INTER_NEAREST);
      // 半透明绿色叠加：只在掩码为 255 的像素上混色。
      cv::Mat colored = canvas->clone();
      colored.setTo(cv::Scalar(0, 200, 0), scaled);
      cv::addWeighted(colored, 0.45, *canvas, 0.55, 0.0, *canvas);
      ++overlaid;
    }
    const cv::Rect box(static_cast<int>(item.x), static_cast<int>(item.y),
                       static_cast<int>(item.width), static_cast<int>(item.height));
    // 检测框可能超出画面边界，先与图像范围求交再绘制。
    const cv::Rect clipped = box & cv::Rect(0, 0, canvas->cols, canvas->rows);
    if (clipped.area() <= 0)
      continue;
    cv::rectangle(*canvas, clipped, cv::Scalar(0, 0, 255), 3);
    cv::circle(*canvas,
               cv::Point(static_cast<int>(item.centroid_x), static_cast<int>(item.centroid_y)), 20,
               cv::Scalar(255, 255, 255), -1);
    char label[128];
    std::snprintf(label, sizeof(label), "#%zu leak %.3f area %u", i, item.score, item.area);
    cv::putText(*canvas, label, cv::Point(clipped.x, std::max(28, clipped.y - 12)),
                cv::FONT_HERSHEY_SIMPLEX, 1.6, cv::Scalar(0, 0, 255), 3);
  }
  char banner[192];
  std::snprintf(banner, sizeof(banner), "alert=%s leaks=%u hits=%u area=%u",
                AlertLevelName(state.level), state.leak_count, state.hits, state.total_area);
  const cv::Scalar color =
      state.level == CVSDK_LEAK_ALERT_NONE ? cv::Scalar(0, 160, 0) : cv::Scalar(0, 0, 255);
  cv::putText(*canvas, banner, cv::Point(24, 60), cv::FONT_HERSHEY_SIMPLEX, 1.8, color, 4);
  return overlaid;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <model_package> <image> [output.jpg]\n";
    return 2;
  }
  const std::string model_package = argv[1];
  const std::string image_path = argv[2];
  const std::string output_path = argc > 3 ? argv[3] : "output.jpg";

  // options 传 NULL 时，规则文件默认为 <model_package>/leak_rules.json。
  CVSDK_LeakProcessor* processor = nullptr;
  if (CVSDK_LeakProcessorCreate(model_package.c_str(), nullptr, &processor) != CVSDK_OK) {
    std::cerr << "processor create failed: " << CVSDK_GetLastError() << '\n';
    return 1;
  }
  const cv::Mat image = cv::imread(image_path);
  if (image.empty()) {
    std::cerr << "cannot read image: " << image_path << '\n';
    CVSDK_LeakProcessorDestroy(processor);
    return 3;
  }
  const CVSDK_Image input{sizeof(input), image.data, static_cast<uint32_t>(image.cols),
                          static_cast<uint32_t>(image.rows), static_cast<uint32_t>(image.step),
                          CVSDK_PIXEL_FORMAT_BGR8};
  // Process 有状态并会推进滑动窗口，因此固定预分配容量、每帧只调用一次。
  CVSDK_LeakItem storage[16];
  CVSDK_LeakItemList list{sizeof(list), storage, 16, 0};
  CVSDK_LeakAlertState state{sizeof(state)};
  const CVSDK_Status status = CVSDK_LeakProcessorProcess(processor, &input, &list, &state);
  if (status != CVSDK_OK && status != CVSDK_BUFFER_TOO_SMALL) {
    std::cerr << "inference failed: " << CVSDK_GetLastError() << '\n';
    CVSDK_LeakProcessorDestroy(processor);
    return 1;
  }
  // 容量不足时 SDK 不写入明细，此时只展示告警状态。
  const uint32_t count = status == CVSDK_OK ? list.count : 0U;
  const std::vector<CVSDK_LeakItem> items(storage, storage + count);

  std::cout << "leaks=" << count << " level=" << AlertLevelName(state.level)
            << " hits=" << state.hits << " window_filled=" << state.window_filled
            << " total_area=" << state.total_area << " reason=" << state.reason << '\n';
  for (size_t i = 0; i < items.size(); ++i) {
    const CVSDK_LeakItem& item = items[i];
    std::cout << std::fixed << std::setprecision(4) << "  [" << i << "] score=" << item.score
              << " box=[" << item.x << ',' << item.y << ',' << item.width << ',' << item.height
              << "] centroid=[" << item.centroid_x << ',' << item.centroid_y << ']'
              << " area=" << item.area << '\n';
  }

  // 掩码只在当前帧有效，必须在销毁处理器之前取走。
  cv::Mat canvas = image.clone();
  const uint32_t overlaid = Draw(processor, items, state, &canvas);
  CVSDK_LeakProcessorDestroy(processor);

  if (!cv::imwrite(output_path, canvas)) {
    std::cerr << "cannot write " << output_path << '\n';
    return 1;
  }
  std::cout << "saved " << output_path << " (" << canvas.cols << 'x' << canvas.rows
            << "), masks overlaid=" << overlaid << '\n';
  return 0;
}
