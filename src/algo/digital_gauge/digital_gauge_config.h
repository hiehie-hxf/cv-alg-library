#pragma once

#include "cv_sdk/cv_sdk.h"
#include <string>

namespace cvsdk {

/** 七段数码管识别的设备标定参数；默认值对应 gauge_ocr 基准设备。 */
struct DigitalGaugeConfig {
  // 发光数码管的 HSV 分割阈值。
  int led_value_min = 130;
  int led_saturation_min = 80;
  int red_hue_low_max = 12;
  int red_hue_high_min = 168;
  int green_hue_min = 40;
  int green_hue_max = 95;

  // 蓝色面板的 HSV 阈值，以及相对输入尺寸的几何过滤条件。
  int panel_hue_min = 100;
  int panel_hue_max = 135;
  int panel_saturation_min = 110;
  int panel_value_min = 80;
  double panel_min_area_ratio = 20000.0 / (4096.0 * 3072.0);
  double panel_max_width_ratio = 800.0 / 4096.0;
  double panel_max_y_ratio = 1900.0 / 3072.0;

  // 七段码判定、行高分档和斜体网格搜索参数。
  int base_threshold = 130;
  int large_digit_threshold = 145;
  int small_digit_threshold = 205;
  int large_digit_height_split = 78;
  double on_fraction = 0.55;
  double minimum_specificity = 0.25;
  double shear_min = -0.40;
  double shear_max = 0.40;
  double shear_step = 0.01;
  int minimum_digits = 3;
  int maximum_digits = 6;
};

/** 从模型包目录加载 reader_config.json，并验证关键参数范围。 */
CVSDK_Status LoadDigitalGaugeConfig(const std::string& package_dir, DigitalGaugeConfig* output);

} // namespace cvsdk
