#include "cv_sdk/cv_sdk.h"
// 单元测试验证配置加载、空指针防护、容量查询协议、Reset 语义和小目标过滤。
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <vector>

int main() {
  CVSDK_LeakProcessor* processor = nullptr;
  CVSDK_LeakAlertState state{sizeof(state)};

  // 参数校验：空指针必须被拒绝，而不是崩溃。
  assert(CVSDK_LeakProcessorCreate(nullptr, nullptr, &processor) == CVSDK_INVALID_ARGUMENT);
  assert(CVSDK_LeakProcessorCreate("models/leak_seg_1280", nullptr, nullptr) ==
         CVSDK_INVALID_ARGUMENT);
  assert(CVSDK_LeakProcessorReset(nullptr) == CVSDK_INVALID_ARGUMENT);

  // 规则文件缺失时按参数错误处理，与 FireSmokeProcessorCreate 的行为保持一致。
  assert(CVSDK_LeakProcessorCreate("models/does_not_exist", nullptr, &processor) ==
         CVSDK_INVALID_ARGUMENT);

  // 配置加载：不传 options 时使用模型包同目录的 leak_rules.json。
  CVSDK_Status status = CVSDK_LeakProcessorCreate("models/leak_seg_1280", nullptr, &processor);
  assert(status == CVSDK_OK);

  const cv::Mat image = cv::imread("tests/data/leak/leak01.jpg");
  if (image.empty()) {
    std::fprintf(stderr, "leak test image is unavailable\n");
    return 2;
  }
  const CVSDK_Image input{sizeof(input), image.data, static_cast<uint32_t>(image.cols),
                          static_cast<uint32_t>(image.rows), static_cast<uint32_t>(image.step),
                          CVSDK_PIXEL_FORMAT_BGR8};

  // 容量查询协议：items=NULL 时写入所需数量，非空结果返回 BUFFER_TOO_SMALL。
  CVSDK_LeakItemList query{sizeof(query), nullptr, 0, 0};
  status = CVSDK_LeakProcessorProcess(processor, &input, &query, &state);
  assert(status == (query.count > 0 ? CVSDK_BUFFER_TOO_SMALL : CVSDK_OK));
  assert(state.leak_count == query.count);
  // 预热期只累计命中数，不做趋势判定。
  assert(state.window_filled == 0 && state.area_growing == 0 && state.centroid_down == 0);
  assert(state.level == CVSDK_LEAK_ALERT_NONE);

  // 正常取数：预分配足够容量，只调用一次。
  std::vector<CVSDK_LeakItem> items(16);
  CVSDK_LeakItemList list{sizeof(list), items.data(), static_cast<uint32_t>(items.size()), 0};
  status = CVSDK_LeakProcessorProcess(processor, &input, &list, &state);
  assert(status == CVSDK_OK && list.count <= items.size());

  // 容量不足时必须被拒绝，且不写越界。
  CVSDK_LeakItemList tight{sizeof(tight), items.data(), 0, 0};
  if (list.count > 0) {
    status = CVSDK_LeakProcessorProcess(processor, &input, &tight, &state);
    assert(status == CVSDK_BUFFER_TOO_SMALL && tight.count == list.count);
  }

  // Reset 后窗口清空，重新回到预热态。
  assert(CVSDK_LeakProcessorReset(processor) == CVSDK_OK);
  status = CVSDK_LeakProcessorProcess(processor, &input, &list, &state);
  assert(status == CVSDK_OK);
  assert(state.window_filled == 0 && state.level == CVSDK_LEAK_ALERT_NONE);
  assert(state.hits == (list.count > 0 ? 1U : 0U));

  // 有状态接口收到空句柄时返回参数错误。
  assert(CVSDK_LeakProcessorProcess(nullptr, &input, &list, &state) == CVSDK_INVALID_ARGUMENT);
  CVSDK_LeakProcessorDestroy(processor);

  // min_area_ratio 边界：极端阈值应过滤掉全部候选。
  CVSDK_LeakProcessorOptions options{sizeof(options), "onnxruntime", nullptr, 0.F, .99F, {0}};
  status = CVSDK_LeakProcessorCreate("models/leak_seg_1280", &options, &processor);
  assert(status == CVSDK_OK);
  status = CVSDK_LeakProcessorProcess(processor, &input, &list, &state);
  assert(status == CVSDK_OK && list.count == 0 && state.leak_count == 0);
  CVSDK_LeakProcessorDestroy(processor);
  CVSDK_LeakProcessorDestroy(nullptr);

  std::cout << "leak_processor_test passed\n";
  return 0;
}
