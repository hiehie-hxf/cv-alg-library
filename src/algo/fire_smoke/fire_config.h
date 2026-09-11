#pragma once

#include "cv_sdk/cv_sdk.h"
#include <string>

namespace cvsdk {
/** 火情算法运行参数。字段名称与 JSON schema 一一对应，加载后不再依赖字符串查询。 */
struct FireConfig {
  float min_area_ratio = .0005F;
  float fire_candidate_conf = .25F;
  float smoke_candidate_conf = .10F;
  uint32_t fire_window = 5, fire_min_hits = 3;
  float fire_confirm_conf = .40F, critical_fire_conf = .75F;
  uint32_t fire_strong_min_hits = 2, critical_fire_consecutive = 2;
  float track_iou_threshold = .30F;
  uint32_t track_max_missed = 1;
  uint32_t smoke_window = 10, smoke_min_hits = 3;
  float smoke_confirm_conf = .22F;
  bool fire_color_gate_enabled = true;
  float fire_color_min_fraction = .08F;
  bool smoke_static_gate_enabled = true;
  float smoke_static_diff = 2.5F, smoke_static_ratio = 2.0F, smoke_bypass_conf = .80F;
  float smoke_overexposed_max = .50F, smoke_halo_mean = 190.F, smoke_halo_core_frac = .04F,
        smoke_halo_core_mean = 150.F;
  uint32_t smoke_soft_active_min = 25;
  float smoke_global_motion_max_ratio = .35F;
};
/** 输入：JSON 文件路径；输出：填充强类型配置并返回校验状态。 */
CVSDK_Status LoadFireConfig(const char* path, FireConfig* config);
} // namespace cvsdk
