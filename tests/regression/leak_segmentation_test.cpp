#include "cv_sdk/cv_sdk.h"
// 回归测试锁定真实模型在固定测试图上的检出数量与掩码面积基线，并输出可视化结果。
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>

namespace {

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
  // stride 等于宽度，可以直接包一层 Mat；clone 出独立内存避免悬垂。
  return cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC1, buffer.data()).clone();
}

} // namespace

int main() {
  const cv::Mat image = cv::imread("tests/data/leak/leak01.jpg");
  if (image.empty()) {
    std::fprintf(stderr, "leak regression image is unavailable\n");
    return 2;
  }
  CVSDK_LeakProcessor* processor = nullptr;
  if (CVSDK_LeakProcessorCreate("models/leak_seg_1280", nullptr, &processor) != CVSDK_OK) {
    std::fprintf(stderr, "%s\n", CVSDK_GetLastError());
    return 1;
  }
  const CVSDK_Image input{sizeof(input), image.data, static_cast<uint32_t>(image.cols),
                          static_cast<uint32_t>(image.rows), static_cast<uint32_t>(image.step),
                          CVSDK_PIXEL_FORMAT_BGR8};
  // Process 有状态并会推进滑动窗口，因此固定预分配容量、只调用一次。
  std::vector<CVSDK_LeakItem> items(16);
  CVSDK_LeakItemList list{sizeof(list), items.data(), static_cast<uint32_t>(items.size()), 0};
  CVSDK_LeakAlertState state{sizeof(state)};
  const CVSDK_Status status = CVSDK_LeakProcessorProcess(processor, &input, &list, &state);
  if (status != CVSDK_OK) {
    std::fprintf(stderr, "leak inference failed: %d %s\n", status, CVSDK_GetLastError());
    CVSDK_LeakProcessorDestroy(processor);
    return 1;
  }
  std::fprintf(stdout, "count=%u area0=%u centroid_y0=%.2f confidence0=%.4f hits=%u\n", list.count,
               list.count ? items[0].area : 0U, list.count ? items[0].centroid_y : 0.F,
               list.count ? items[0].score : 0.F, state.hits);
  if (list.count != 1) {
    std::fprintf(stderr, "unexpected detection count: %u\n", list.count);
    CVSDK_LeakProcessorDestroy(processor);
    return 1;
  }
  // ONNX package baseline for tests/data/leak/leak01.jpg with the default rules.
  if (std::abs(static_cast<double>(items[0].area) - 1587632.0) > 16000.0 ||
      std::abs(items[0].centroid_y - 1385.43F) > 20.F ||
      std::abs(items[0].score - 0.5696F) > 0.01F || state.total_area != items[0].area) {
    std::fprintf(stderr,
                 "unexpected mask baseline: area=%u centroid_y=%.2f score=%.4f total=%u\n",
                 items[0].area, items[0].centroid_y, items[0].score, state.total_area);
    CVSDK_LeakProcessorDestroy(processor);
    return 1;
  }

  // 可视化：掩码只在当前帧有效，必须在销毁处理器之前取走。
  cv::Mat canvas = image.clone();
  uint32_t overlaid = 0;
  for (uint32_t i = 0; i < list.count; ++i) {
    const CVSDK_LeakItem& item = items[i];
    const cv::Mat mask = FetchMask(processor, i);
    if (!mask.empty()) {
      cv::Mat scaled = mask;
      if (mask.size() != image.size())
        cv::resize(mask, scaled, image.size(), 0, 0, cv::INTER_NEAREST);
      // 半透明绿色叠加：只在掩码为 255 的像素上混色。
      cv::Mat colored = canvas.clone();
      colored.setTo(cv::Scalar(0, 200, 0), scaled);
      cv::addWeighted(colored, 0.45, canvas, 0.55, 0.0, canvas);
      ++overlaid;
    }
    const cv::Rect box(static_cast<int>(item.x), static_cast<int>(item.y),
                       static_cast<int>(item.width), static_cast<int>(item.height));
    // 检测框可能超出画面边界，先与图像范围求交再绘制。
    const cv::Rect clipped = box & cv::Rect(0, 0, canvas.cols, canvas.rows);
    if (clipped.area() <= 0)
      continue;
    cv::rectangle(canvas, clipped, cv::Scalar(0, 0, 255), 3);
    cv::circle(
        canvas,
        cv::Point(static_cast<int>(item.centroid_x), static_cast<int>(item.centroid_y)), 20,
        cv::Scalar(255, 255, 255), -1);
    char label[128];
    std::snprintf(label, sizeof(label), "#%u leak %.3f area %u", i, item.score, item.area);
    cv::putText(canvas, label, cv::Point(clipped.x, std::max(28, clipped.y - 12)),
                cv::FONT_HERSHEY_SIMPLEX, 1.6, cv::Scalar(0, 0, 255), 3);
  }
  CVSDK_LeakProcessorDestroy(processor);

  const std::string result_path = "tests/data/leak/leak01_result.jpg";
  if (!cv::imwrite(result_path, canvas)) {
    std::fprintf(stderr, "cannot write visualization: %s\n", result_path.c_str());
    return 1;
  }
  std::fprintf(stdout, "visualization: %s (%dx%d), masks overlaid=%u\n", result_path.c_str(),
               canvas.cols, canvas.rows, overlaid);
  return 0;
}
