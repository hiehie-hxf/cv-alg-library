#include "cv_sdk/cv_sdk.h"
// 长稳压测入口；建议配合 ASan/LSan、RSS 和句柄监控运行数小时。
#include <cassert>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
  const unsigned long iterations = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 1000000UL;
  CVSDK_FireFilter* filter = nullptr;
  assert(CVSDK_FireFilterCreate("models/fire_smoke_1280/fire_rules.json", &filter) == CVSDK_OK);
  CVSDK_Detection detection{100, 100, 80, 80, .50F, 1};
  CVSDK_FireAlertState state{sizeof(state)};
  for (unsigned long i = 0; i < iterations; ++i) {
    const CVSDK_Detection* input = (i % 7 == 0) ? nullptr : &detection;
    const uint32_t count = input ? 1 : 0;
    assert(CVSDK_FireFilterProcess(filter, 1280, 720, input, count, &state) == CVSDK_OK);
  }
  CVSDK_FireFilterDestroy(filter);
  std::cout << "stress completed: " << iterations << " frames\n";
  return 0;
}
