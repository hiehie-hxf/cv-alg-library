#include "algo/fire_smoke/fire_filter.h"

#include "base/status.h"
#include <algorithm>
#include <cstdio>
#include <limits>

// 告警器只消费已完成图像门控的检测框；模型推理和业务规则因此可以独立回归测试。

namespace cvsdk {
float FireFilter::IoU(const CVSDK_Detection& lhs, const CVSDK_Detection& rhs) {
  const float left = std::max(lhs.x, rhs.x);
  const float top = std::max(lhs.y, rhs.y);
  const float right = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const float bottom = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  const float intersection = std::max(0.F, right - left) * std::max(0.F, bottom - top);
  const float union_area = lhs.width * lhs.height + rhs.width * rhs.height - intersection;
  return union_area > 0.F ? intersection / union_area : 0.F;
}

void FireFilter::PushScore(Track* track, uint32_t window, float score) {
  track->scores.push_back(score);
  while (track->scores.size() > window)
    track->scores.pop_front();
}

void FireFilter::UpdateTracks(int32_t class_id,
                              const std::vector<CVSDK_Detection>& detections, uint32_t window,
                              float candidate_conf) {
  auto& tracks = class_id == 1 ? fire_tracks_ : smoke_tracks_;
  std::vector<bool> matched(tracks.size(), false);

  // Greedy association is sufficient here because each frame has already been NMS'ed.
  for (const auto& detection : detections) {
    size_t best = tracks.size();
    float best_iou = config_.track_iou_threshold;
    for (size_t i = 0; i < tracks.size(); ++i) {
      if (matched[i])
        continue;
      const float iou = IoU(tracks[i].box, detection);
      if (iou >= best_iou) {
        best_iou = iou;
        best = i;
      }
    }
    if (best == tracks.size()) {
      Track track;
      track.box = detection;
      PushScore(&track, window, detection.score >= candidate_conf ? detection.score : 0.F);
      tracks.push_back(std::move(track));
      matched.push_back(true);
    } else {
      Track& track = tracks[best];
      track.box = detection;
      track.missed = 0;
      PushScore(&track, window, detection.score >= candidate_conf ? detection.score : 0.F);
      matched[best] = true;
    }
  }

  for (size_t i = 0; i < tracks.size(); ++i) {
    if (matched[i])
      continue;
    ++tracks[i].missed;
    PushScore(&tracks[i], window, 0.F);
  }
  tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                              [this](const Track& track) {
                                return track.missed > config_.track_max_missed;
                              }),
               tracks.end());
}

uint32_t FireFilter::Hits(const Track& track) {
  return static_cast<uint32_t>(
      std::count_if(track.scores.begin(), track.scores.end(), [](float x) { return x > 0.F; }));
}

float FireFilter::Max(const Track& track) {
  return track.scores.empty() ? 0.F : *std::max_element(track.scores.begin(), track.scores.end());
}

uint32_t FireFilter::StrongHits(const Track& track, float threshold) {
  return static_cast<uint32_t>(std::count_if(
      track.scores.begin(), track.scores.end(), [threshold](float x) { return x >= threshold; }));
}

uint32_t FireFilter::ConsecutiveStrongHits(const Track& track, float threshold) {
  uint32_t count = 0;
  for (auto it = track.scores.rbegin(); it != track.scores.rend() && *it >= threshold; ++it)
    ++count;
  return count;
}

bool FireFilter::Confirmed(const Track& track, uint32_t min_hits, float confirm_conf) {
  return Hits(track) >= min_hits && Max(track) >= confirm_conf;
}

const FireFilter::Track* FireFilter::BestTrack(const std::vector<Track>& tracks) const {
  const Track* best = nullptr;
  for (const auto& track : tracks) {
    if (!best || Hits(track) > Hits(*best) ||
        (Hits(track) == Hits(*best) && Max(track) > Max(*best)))
      best = &track;
  }
  return best;
}

void FireFilter::Reset() {
  fire_tracks_.clear();
  smoke_tracks_.clear();
}
CVSDK_Status FireFilter::Process(uint32_t width, uint32_t height, const CVSDK_Detection* detections,
                                 uint32_t count, CVSDK_FireAlertState* state) {
  if (!state || state->struct_size < sizeof(CVSDK_FireAlertState) || width == 0 || height == 0 ||
      (count && !detections)) {
    SetLastError("invalid fire filter input");
    return CVSDK_INVALID_ARGUMENT;
  }
  const float image_area = static_cast<float>(width) * height;
  std::vector<CVSDK_Detection> fire, smoke;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& d = detections[i];
    if (d.score < 0 || d.score > 1 || d.width < 0 || d.height < 0)
      continue;
    const float area_ratio = d.width * d.height / image_area;
    if (area_ratio < config_.min_area_ratio)
      continue;
    if (d.class_id == 1)
      fire.push_back(d);
    else if (d.class_id == 0)
      smoke.push_back(d);
  }
  UpdateTracks(1, fire, config_.fire_window, config_.fire_candidate_conf);
  UpdateTracks(0, smoke, config_.smoke_window, config_.smoke_candidate_conf);

  const Track* best_fire = BestTrack(fire_tracks_);
  const Track* best_smoke = BestTrack(smoke_tracks_);
  const uint32_t fire_hits = best_fire ? Hits(*best_fire) : 0;
  const uint32_t smoke_hits = best_smoke ? Hits(*best_smoke) : 0;
  const float fire_max = best_fire ? Max(*best_fire) : 0.F;
  const float smoke_max = best_smoke ? Max(*best_smoke) : 0.F;
  const bool fire_confirmed =
      best_fire && Confirmed(*best_fire, config_.fire_min_hits, config_.fire_confirm_conf) &&
      StrongHits(*best_fire, config_.fire_confirm_conf) >= config_.fire_strong_min_hits;
  const bool smoke_confirmed =
      best_smoke && Confirmed(*best_smoke, config_.smoke_min_hits, config_.smoke_confirm_conf);
  const bool critical =
      best_fire && ConsecutiveStrongHits(*best_fire, config_.critical_fire_conf) >=
                       config_.critical_fire_consecutive;
  state->level = CVSDK_FIRE_ALERT_NONE;
  const char* reason = "no_detection";
  if (critical || (fire_confirmed && smoke_confirmed)) {
    state->level = CVSDK_FIRE_ALERT_CRITICAL;
    reason = critical ? "fire_high_confidence_sustained" : "fire_and_smoke_confirmed";
  } else if (fire_confirmed) {
    state->level = CVSDK_FIRE_ALERT_WARNING_FIRE;
    reason = "fire_confirmed";
  } else if (smoke_confirmed) {
    state->level = CVSDK_FIRE_ALERT_WARNING_SMOKE;
    reason = "smoke_confirmed";
  } else if (fire_hits || smoke_hits) {
    state->level = CVSDK_FIRE_ALERT_INFO;
    reason = "candidate_detected";
  }
  state->max_fire_confidence = fire_max;
  state->max_smoke_confidence = smoke_max;
  state->fire_hits = fire_hits;
  state->smoke_hits = smoke_hits;
  std::snprintf(state->reason, sizeof(state->reason), "%s", reason);
  return CVSDK_OK;
}
} // namespace cvsdk
