#include "algo/digital_gauge/digital_gauge_config.h"

#include "base/json.h"
#include "base/status.h"
#include <filesystem>

namespace cvsdk {
namespace {
void ReadNumber(const json::Value& root, const char* path, double* value) {
  double parsed = 0;
  if (json::Number(json::FindPath(root, path), &parsed))
    *value = parsed;
}
void ReadInt(const json::Value& root, const char* path, int* value) {
  double parsed = 0;
  if (json::Number(json::FindPath(root, path), &parsed))
    *value = static_cast<int>(parsed);
}
} // namespace

CVSDK_Status LoadDigitalGaugeConfig(const std::string& package_dir, DigitalGaugeConfig* output) {
  if (!output || package_dir.empty()) {
    SetLastError("digital gauge package and output are required");
    return CVSDK_INVALID_ARGUMENT;
  }
  const auto manifest = std::filesystem::path(package_dir) / "manifest.json";
  const auto config_path = std::filesystem::path(package_dir) / "reader_config.json";
  if (!std::filesystem::is_regular_file(manifest) ||
      !std::filesystem::is_regular_file(config_path)) {
    SetLastError("digital gauge package requires manifest.json and reader_config.json");
    return CVSDK_NOT_FOUND;
  }
  json::Value root;
  std::string error;
  if (!json::ParseFile(config_path.string(), &root, &error)) {
    SetLastError(error);
    return CVSDK_INVALID_ARGUMENT;
  }
  DigitalGaugeConfig config;
  ReadInt(root, "led.value_min", &config.led_value_min);
  ReadInt(root, "led.saturation_min", &config.led_saturation_min);
  ReadInt(root, "panel.hue_min", &config.panel_hue_min);
  ReadInt(root, "panel.hue_max", &config.panel_hue_max);
  ReadInt(root, "panel.saturation_min", &config.panel_saturation_min);
  ReadInt(root, "panel.value_min", &config.panel_value_min);
  ReadNumber(root, "panel.min_area_ratio", &config.panel_min_area_ratio);
  ReadNumber(root, "panel.max_width_ratio", &config.panel_max_width_ratio);
  ReadNumber(root, "panel.max_y_ratio", &config.panel_max_y_ratio);
  ReadInt(root, "display.base_threshold", &config.base_threshold);
  ReadInt(root, "display.large_digit_threshold", &config.large_digit_threshold);
  ReadInt(root, "display.small_digit_threshold", &config.small_digit_threshold);
  ReadInt(root, "display.large_digit_height_split", &config.large_digit_height_split);
  ReadNumber(root, "display.on_fraction", &config.on_fraction);
  ReadNumber(root, "display.minimum_specificity", &config.minimum_specificity);
  ReadNumber(root, "geometry.shear_min", &config.shear_min);
  ReadNumber(root, "geometry.shear_max", &config.shear_max);
  ReadNumber(root, "geometry.shear_step", &config.shear_step);
  ReadInt(root, "geometry.minimum_digits", &config.minimum_digits);
  ReadInt(root, "geometry.maximum_digits", &config.maximum_digits);
  if (config.panel_hue_min < 0 || config.panel_hue_max > 180 ||
      config.panel_hue_min >= config.panel_hue_max || config.on_fraction <= 0 ||
      config.on_fraction >= 1 || config.shear_step <= 0 || config.minimum_digits < 1 ||
      config.maximum_digits < config.minimum_digits) {
    SetLastError("invalid digital gauge reader configuration");
    return CVSDK_INVALID_ARGUMENT;
  }
  *output = config;
  return CVSDK_OK;
}
} // namespace cvsdk
