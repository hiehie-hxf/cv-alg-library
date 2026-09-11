#include "cv_sdk/cv_sdk.h"
// 单元测试验证 OpenCV 颜色门控：暖色火焰通过，黑色伪目标被过滤。
#include <cassert>
#include <iostream>
#include <vector>
int main() {
  CVSDK_FireSmokeProcessor* processor = nullptr;
  assert(CVSDK_FireSmokeProcessorCreate("models/fire_smoke_640/fire_rules.json", &processor) ==
         CVSDK_OK);
  std::vector<unsigned char> image(100 * 100 * 3, 0);
  // BGR orange rectangle: B=0 G=170 R=255. This should pass the fire color gate.
  for (int y = 20; y < 80; ++y)
    for (int x = 20; x < 80; ++x) {
      auto* p = image.data() + (y * 100 + x) * 3;
      p[0] = 0;
      p[1] = 170;
      p[2] = 255;
    }
  CVSDK_Image frame{sizeof(frame), image.data(), 100, 100, 300, CVSDK_PIXEL_FORMAT_BGR8};
  CVSDK_Detection fire{20, 20, 60, 60, .60F, 1};
  CVSDK_Detection out[2];
  CVSDK_DetectionList list{sizeof(list), out, 2, 0};
  CVSDK_FireAlertState state{sizeof(state)};
  for (int i = 0; i < 3; ++i)
    assert(CVSDK_FireSmokeProcessorProcess(processor, &frame, &fire, 1, &list, &state) == CVSDK_OK);
  assert(list.count == 1 && state.level == CVSDK_FIRE_ALERT_WARNING_FIRE);
  CVSDK_Detection low_fire{20, 20, 60, 60, .12F, 1};
  assert(CVSDK_FireSmokeProcessorReset(processor) == CVSDK_OK);
  assert(CVSDK_FireSmokeProcessorProcess(processor, &frame, &low_fire, 1, &list, &state) ==
             CVSDK_OK &&
         list.count == 0);
  std::fill(image.begin(), image.end(), 0);
  assert(CVSDK_FireSmokeProcessorReset(processor) == CVSDK_OK);
  assert(CVSDK_FireSmokeProcessorProcess(processor, &frame, &fire, 1, &list, &state) == CVSDK_OK &&
         list.count == 0);
  CVSDK_FireSmokeProcessorDestroy(processor);
  std::cout << "fire_smoke_processor_test passed\n";
}
