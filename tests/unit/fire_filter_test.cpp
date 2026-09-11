#include "cv_sdk/cv_sdk.h"
// 单元测试验证 JSON 配置加载、同目标时序确认和小目标过滤。
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
  CVSDK_FireFilter* filter = nullptr;
  assert(CVSDK_FireFilterCreate("models/fire_smoke_640/fire_rules.json", &filter) == CVSDK_OK);
  CVSDK_Detection fire{100, 100, 50, 50, .60F, 1};
  CVSDK_FireAlertState state{sizeof(state)};
  for (int i = 0; i < 3; ++i)
    assert(CVSDK_FireFilterProcess(filter, 1280, 720, &fire, 1, &state) == CVSDK_OK);
  assert(state.level == CVSDK_FIRE_ALERT_WARNING_FIRE && state.fire_hits == 3 &&
         std::strcmp(state.reason, "fire_confirmed") == 0);

  // Different locations must not be combined into one fire event.
  CVSDK_Detection elsewhere{900, 500, 50, 50, .60F, 1};
  assert(CVSDK_FireFilterReset(filter) == CVSDK_OK);
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &fire, 1, &state) == CVSDK_OK);
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &elsewhere, 1, &state) == CVSDK_OK);
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &fire, 1, &state) == CVSDK_OK);
  assert(state.level != CVSDK_FIRE_ALERT_WARNING_FIRE);

  // One high-confidence outlier must not trigger critical.
  assert(CVSDK_FireFilterReset(filter) == CVSDK_OK);
  CVSDK_Detection high{100, 100, 50, 50, .90F, 1};
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &high, 1, &state) == CVSDK_OK);
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &fire, 1, &state) == CVSDK_OK);
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &fire, 1, &state) == CVSDK_OK);
  assert(state.level != CVSDK_FIRE_ALERT_CRITICAL);
  CVSDK_Detection too_small{0, 0, 1, 1, .99F, 1};
  assert(CVSDK_FireFilterReset(filter) == CVSDK_OK);
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &too_small, 1, &state) == CVSDK_OK &&
         state.level == CVSDK_FIRE_ALERT_NONE);
  CVSDK_FireFilterDestroy(filter);
  std::cout << "fire_filter_test passed\n";
}
