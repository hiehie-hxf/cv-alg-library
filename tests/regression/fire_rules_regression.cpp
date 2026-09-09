#include "cv_sdk/cv_sdk.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  CVSDK_FireFilter* filter = nullptr;
  assert(CVSDK_FireFilterCreate("models/fire_smoke_1280/fire_rules.json", &filter) == CVSDK_OK);
  CVSDK_Detection smoke{20, 20, 120, 80, .50F, 0};
  CVSDK_Detection fire{100, 100, 100, 100, .80F, 1};
  CVSDK_FireAlertState state{sizeof(state)};

  // Golden case: three sustained high-confidence fire frames -> critical.
  for (int i = 0; i < 3; ++i)
    assert(CVSDK_FireFilterProcess(filter, 1280, 720, &fire, 1, &state) == CVSDK_OK);
  assert(state.level == CVSDK_FIRE_ALERT_CRITICAL);
  assert(std::strcmp(state.reason, "fire_high_confidence_sustained") == 0);

  // Golden case: after reset, smoke needs 3 hits in its own 10-frame window.
  assert(CVSDK_FireFilterReset(filter) == CVSDK_OK);
  for (int i = 0; i < 2; ++i)
    assert(CVSDK_FireFilterProcess(filter, 1280, 720, &smoke, 1, &state) == CVSDK_OK);
  assert(state.level == CVSDK_FIRE_ALERT_INFO);
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &smoke, 1, &state) == CVSDK_OK);
  assert(state.level == CVSDK_FIRE_ALERT_WARNING_SMOKE);
  CVSDK_FireFilterDestroy(filter);
  std::cout << "fire_rules_regression passed\n";
  return 0;
}
