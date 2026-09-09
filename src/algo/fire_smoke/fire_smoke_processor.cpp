#include "algo/fire_smoke/fire_smoke_processor.h"
#include "algo/fire_smoke/demo_torch_gate.h"
#include "base/status.h"

namespace cvsdk {
CVSDK_Status FireSmokeProcessor::Process(const CVSDK_Image& image, const CVSDK_Detection* input,
                                         uint32_t count, std::vector<CVSDK_Detection>* filtered,
                                         CVSDK_FireAlertState* state) {
  if (!filtered || !state || state->struct_size < sizeof(CVSDK_FireAlertState) || !image.data ||
      !image.width || !image.height || (count && !input)) {
    SetLastError("invalid fire/smoke processor input");
    return CVSDK_INVALID_ARGUMENT;
  }
  filtered->clear();
  filtered->reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& detection = input[i];
    // Keep the low smoke candidate threshold, but do not display fire boxes
    // below the fire-specific candidate threshold. The temporal filter uses
    // the same thresholds, so display and alert decisions stay consistent.
    if (detection.class_id == 1 && detection.score < config_.fire_candidate_conf)
      continue;
    if (detection.class_id == 0 && detection.score < config_.smoke_candidate_conf)
      continue;
    if (detection.class_id == 1 && config_.fire_color_gate_enabled &&
        !PassFireColorGate(image, detection, config_.fire_color_min_fraction))
      continue;
    filtered->push_back(detection);
  }
  auto demo = DetectDemoTorch(image, config_);
  filtered->insert(filtered->end(), demo.begin(), demo.end());
  CVSDK_Status gate_status = smoke_gate_.Filter(image, filtered);
  if (gate_status != CVSDK_OK)
    return gate_status;
  return filter_.Process(image.width, image.height, filtered->data(),
                         static_cast<uint32_t>(filtered->size()), state);
}
} // namespace cvsdk
