#include "algo/fire_smoke/fire_config.h"

#include "base/json.h"
#include "base/status.h"
#include <cmath>

namespace cvsdk {
namespace {
bool ReadFloat(const json::Value& root, const char* path, float* target, std::string* error) {
  double value;
  if (!json::Number(json::FindPath(root, path), &value) || !std::isfinite(value) || value < 0.0 ||
      value > 1.0) {
    *error = std::string("expected number [0,1] at ") + path;
    return false;
  }
  *target = static_cast<float>(value);
  return true;
}
bool ReadCount(const json::Value& root, const char* path, uint32_t* target, std::string* error) {
  double value;
  if (!json::Number(json::FindPath(root, path), &value) || value < 1 || value > 10000 ||
      std::floor(value) != value) {
    *error = std::string("expected positive integer at ") + path;
    return false;
  }
  *target = static_cast<uint32_t>(value);
  return true;
}
bool ReadNonNegative(const json::Value& root, const char* path, float* target, std::string* error) {
  double value;
  if (!json::Number(json::FindPath(root, path), &value) || !std::isfinite(value) || value < 0.0) {
    *error = std::string("expected non-negative number at ") + path;
    return false;
  }
  *target = static_cast<float>(value);
  return true;
}
} // namespace
CVSDK_Status LoadFireConfig(const char* path, FireConfig* config) {
  if (!path || !config) {
    SetLastError("fire config path is required");
    return CVSDK_INVALID_ARGUMENT;
  }
  json::Value root;
  std::string error;
  if (!json::ParseFile(path, &root, &error)) {
    SetLastError(error);
    return CVSDK_INVALID_ARGUMENT;
  }
  double version;
  if (!json::Number(json::FindPath(root, "schema_version"), &version) || version != 1) {
    SetLastError("schema_version must equal 1");
    return CVSDK_INVALID_ARGUMENT;
  }
  FireConfig parsed;
  if (!ReadFloat(root, "postprocess.min_area_ratio", &parsed.min_area_ratio, &error) ||
      !ReadFloat(root, "postprocess.candidate_thresholds.fire", &parsed.fire_candidate_conf,
                 &error) ||
      !ReadFloat(root, "postprocess.candidate_thresholds.smoke", &parsed.smoke_candidate_conf,
                 &error) ||
      !ReadCount(root, "fire_rules.temporal.fire_window", &parsed.fire_window, &error) ||
      !ReadCount(root, "fire_rules.temporal.fire_min_hits", &parsed.fire_min_hits, &error) ||
      !ReadFloat(root, "fire_rules.temporal.fire_confirm_conf", &parsed.fire_confirm_conf,
                 &error) ||
      !ReadFloat(root, "fire_rules.temporal.critical_fire_conf", &parsed.critical_fire_conf,
                 &error) ||
      !ReadCount(root, "fire_rules.temporal.smoke_window", &parsed.smoke_window, &error) ||
      !ReadCount(root, "fire_rules.temporal.smoke_min_hits", &parsed.smoke_min_hits, &error) ||
      !ReadFloat(root, "fire_rules.temporal.smoke_confirm_conf", &parsed.smoke_confirm_conf,
                 &error)) {
    SetLastError(error);
    return CVSDK_INVALID_ARGUMENT;
  }
  bool enabled;
  if (!json::Boolean(json::FindPath(root, "fire_rules.color_gate.enabled"), &enabled) ||
      !ReadFloat(root, "fire_rules.color_gate.min_fraction", &parsed.fire_color_min_fraction,
                 &error)) {
    SetLastError("invalid fire_rules.color_gate configuration");
    return CVSDK_INVALID_ARGUMENT;
  }
  parsed.fire_color_gate_enabled = enabled;
  if (!json::Boolean(json::FindPath(root, "smoke_rules.static_gate.enabled"),
                     &parsed.smoke_static_gate_enabled) ||
      !ReadNonNegative(root, "smoke_rules.static_gate.static_diff", &parsed.smoke_static_diff,
                       &error) ||
      !ReadNonNegative(root, "smoke_rules.static_gate.static_ratio", &parsed.smoke_static_ratio,
                       &error) ||
      !ReadFloat(root, "smoke_rules.static_gate.bypass_conf", &parsed.smoke_bypass_conf, &error) ||
      !ReadFloat(root, "smoke_rules.static_gate.overexposed_max", &parsed.smoke_overexposed_max,
                 &error) ||
      !ReadNonNegative(root, "smoke_rules.static_gate.halo_mean", &parsed.smoke_halo_mean,
                       &error) ||
      !ReadFloat(root, "smoke_rules.static_gate.halo_core_frac", &parsed.smoke_halo_core_frac,
                 &error) ||
      !ReadNonNegative(root, "smoke_rules.static_gate.halo_core_mean", &parsed.smoke_halo_core_mean,
                       &error) ||
      !ReadCount(root, "smoke_rules.static_gate.soft_active_min", &parsed.smoke_soft_active_min,
                 &error)) {
    SetLastError("invalid smoke_rules.static_gate configuration");
    return CVSDK_INVALID_ARGUMENT;
  }
  if (!json::Boolean(json::FindPath(root, "demo_torch.enabled"), &parsed.demo_torch_enabled) ||
      !ReadFloat(root, "demo_torch.min_area_ratio", &parsed.demo_torch_min_area_ratio, &error) ||
      !ReadFloat(root, "demo_torch.min_aspect", &parsed.demo_torch_min_aspect, &error) ||
      !ReadFloat(root, "demo_torch.min_red_fraction", &parsed.demo_torch_min_red_fraction,
                 &error) ||
      !ReadFloat(root, "demo_torch.min_yellow_fraction", &parsed.demo_torch_min_yellow_fraction,
                 &error) ||
      !ReadFloat(root, "demo_torch.confidence", &parsed.demo_torch_confidence, &error)) {
    SetLastError("invalid demo_torch configuration");
    return CVSDK_INVALID_ARGUMENT;
  }
  if (parsed.fire_min_hits > parsed.fire_window || parsed.smoke_min_hits > parsed.smoke_window ||
      parsed.critical_fire_conf < parsed.fire_confirm_conf) {
    SetLastError("invalid temporal rule relationship");
    return CVSDK_INVALID_ARGUMENT;
  }
  *config = parsed;
  return CVSDK_OK;
}
} // namespace cvsdk
