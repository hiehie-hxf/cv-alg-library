#include <iostream>
#include <vector>

#include "leak_detector.h"
#include "leak_filter.h"

int main() {
  leak::LeakConfig config;
  leak::LeakFilter filter(config);

  std::vector<leak::LeakItem> items;
  const leak::LeakAlertState state = filter.Process(items);

  std::cout << "hit_count = " << state.hit_count << std::endl;
  filter.Reset();

  std::cout << "test_leak_filter passed" << std::endl;
  return 0;
}
