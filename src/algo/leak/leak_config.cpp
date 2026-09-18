#include "algo/leak/leak_config.h"

#include "base/json.h"
#include "base/status.h"
#include <cmath>
#include <string>

namespace cvsdk {
// JSON 字段在此一次性转换为强类型配置，并执行范围及字段关系校验。
namespace {
/** 输入：JSON 根、字段路径和目标；输出：读取 [0,1] 浮点数并返回是否成功。 */
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
/** 输入：JSON 根、字段路径和目标；输出：读取正整数并返回是否成功。 */
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
/** 输入：JSON 根、字段路径和目标；输出：读取非负浮点数并返回是否成功。 */
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

CVSDK_Status LoadLeakConfig(const char* path, LeakConfig* config) {
  if (!path || !config) {
    SetLastError("leak config path is required");
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
  LeakConfig parsed;
  if (!ReadFloat(root, "postprocess.min_area_ratio", &parsed.min_area_ratio, &error) ||
      !ReadFloat(root, "postprocess.candidate_conf", &parsed.candidate_conf, &error) ||
      !ReadFloat(root, "postprocess.iou_threshold", &parsed.iou_threshold, &error) ||
      !ReadFloat(root, "postprocess.mask_threshold", &parsed.mask_threshold, &error) ||
      !ReadCount(root, "temporal.window", &parsed.window, &error) ||
      !ReadCount(root, "temporal.min_hits", &parsed.min_hits, &error) ||
      !ReadNonNegative(root, "temporal.area_growth_ratio", &parsed.area_growth_ratio, &error) ||
      !ReadNonNegative(root, "temporal.centroid_down_px", &parsed.centroid_down_px, &error)) {
    SetLastError(error);
    return CVSDK_INVALID_ARGUMENT;
  }
  // 命中数不能超过窗口长度，否则告警永远不会触发。
  if (parsed.min_hits > parsed.window) {
    SetLastError("invalid temporal rule relationship");
    return CVSDK_INVALID_ARGUMENT;
  }
  *config = parsed;
  return CVSDK_OK;
}
} // namespace cvsdk
