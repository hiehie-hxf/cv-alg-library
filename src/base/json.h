#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace cvsdk::json {
struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;
struct Value : std::variant<std::nullptr_t, bool, double, std::string, Object, Array> {
  using variant::variant;
};
bool ParseFile(const std::string& path, Value* out, std::string* error);
const Value* FindPath(const Value& root, const char* path);
bool Number(const Value* value, double* out);
bool Boolean(const Value* value, bool* out);
} // namespace cvsdk::json
