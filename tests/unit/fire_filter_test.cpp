#include "cv_sdk/cv_sdk.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
  CVSDK_FireFilter* filter = nullptr;
  assert(CVSDK_FireFilterCreate("models/fire_smoke_1280/fire_rules.json", &filter) == CVSDK_OK);
  CVSDK_Detection fire{100, 100, 50, 50, .60F, 1};
  CVSDK_FireAlertState state{sizeof(state)};
  for (int i = 0; i < 3; ++i)
    assert(CVSDK_FireFilterProcess(filter, 1280, 720, &fire, 1, &state) == CVSDK_OK);
  assert(state.level == CVSDK_FIRE_ALERT_WARNING_FIRE && state.fire_hits == 3 &&
         std::strcmp(state.reason, "fire_confirmed") == 0);
  CVSDK_Detection too_small{0, 0, 1, 1, .99F, 1};
  assert(CVSDK_FireFilterReset(filter) == CVSDK_OK);
  assert(CVSDK_FireFilterProcess(filter, 1280, 720, &too_small, 1, &state) == CVSDK_OK &&
         state.level == CVSDK_FIRE_ALERT_NONE);
  CVSDK_FireFilterDestroy(filter);
  std::cout << "fire_filter_test passed\n";
}
