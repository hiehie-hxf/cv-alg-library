#pragma once

#include "algo/fire_smoke/fire_config.h"
#include <deque>
#include <vector>

namespace cvsdk {
/**
 * 火焰/烟雾滑动窗口告警器。
 * 该类保存跨帧状态，因此禁止多个摄像头共享同一实例，也不保证并发调用安全。
 */
class FireFilter {
public:
  explicit FireFilter(FireConfig config) : config_(config) {}
  /** 输入：图像尺寸和检测框；输出：更新告警状态及状态码。 */
  CVSDK_Status Process(uint32_t width, uint32_t height, const CVSDK_Detection* detections,
                       uint32_t count, CVSDK_FireAlertState* state);
  /** 输入：无；输出：清空 fire/smoke 滑动窗口。 */
  void Reset();

private:
  struct Track {
    CVSDK_Detection box{};
    std::deque<float> scores;
    uint32_t missed = 0;
  };

  static float IoU(const CVSDK_Detection& lhs, const CVSDK_Detection& rhs);
  static void PushScore(Track* track, uint32_t window, float score);
  void UpdateTracks(int32_t class_id, const std::vector<CVSDK_Detection>& detections,
                    uint32_t window, float candidate_conf);
  static uint32_t Hits(const Track& track);
  static float Max(const Track& track);
  static uint32_t StrongHits(const Track& track, float threshold);
  static uint32_t ConsecutiveStrongHits(const Track& track, float threshold);
  static bool Confirmed(const Track& track, uint32_t min_hits, float confirm_conf);
  const Track* BestTrack(const std::vector<Track>& tracks) const;

  FireConfig config_;
  std::vector<Track> fire_tracks_, smoke_tracks_;
};
} // namespace cvsdk
