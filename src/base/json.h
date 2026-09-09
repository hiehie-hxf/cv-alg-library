#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace cvsdk::json {
/** 仅供 SDK 内部使用的轻量 JSON 值类型，避免公开 API 引入第三方 JSON 类型。 */
struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;
struct Value : std::variant<std::nullptr_t, bool, double, std::string, Object, Array> {
  using variant::variant;
};
/** 输入：JSON 文件路径；输出：解析树和失败文本。 */
bool ParseFile(const std::string& path, Value* out, std::string* error);
const Value* FindPath(const Value& root, const char* path);
bool Number(const Value* value, double* out);
bool Boolean(const Value* value, bool* out);
} // namespace cvsdk::json
