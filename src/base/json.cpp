#include "base/json.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace cvsdk::json {
namespace {
class Parser {
public:
  Parser(const std::string& source, std::string* error) : source_(source), error_(error) {}
  bool Parse(Value* result) {
    Skip();
    if (!ValueOf(result) || (Skip(), pos_ != source_.size()))
      return Fail("invalid JSON");
    return true;
  }

private:
  void Skip() {
    while (pos_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[pos_])))
      ++pos_;
  }
  bool Fail(const char* text) {
    if (error_->empty())
      *error_ = std::string(text) + " at byte " + std::to_string(pos_);
    return false;
  }
  bool Consume(char c) {
    Skip();
    if (pos_ >= source_.size() || source_[pos_] != c)
      return false;
    ++pos_;
    return true;
  }
  bool ValueOf(Value* out) {
    Skip();
    if (pos_ == source_.size())
      return Fail("unexpected end");
    char c = source_[pos_];
    if (c == '{')
      return ObjectOf(out);
    if (c == '[')
      return ArrayOf(out);
    if (c == '"') {
      std::string s;
      if (!String(&s))
        return false;
      *out = std::move(s);
      return true;
    }
    if (c == 't' && source_.compare(pos_, 4, "true") == 0) {
      pos_ += 4;
      *out = true;
      return true;
    }
    if (c == 'f' && source_.compare(pos_, 5, "false") == 0) {
      pos_ += 5;
      *out = false;
      return true;
    }
    if (c == 'n' && source_.compare(pos_, 4, "null") == 0) {
      pos_ += 4;
      *out = nullptr;
      return true;
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
      return NumberOf(out);
    return Fail("unexpected token");
  }
  bool String(std::string* out) {
    if (!Consume('"'))
      return Fail("expected string");
    while (pos_ < source_.size() && source_[pos_] != '"') {
      char c = source_[pos_++];
      if (c == '\\') {
        if (pos_ == source_.size())
          return Fail("bad escape");
        char e = source_[pos_++];
        if (e == '"' || e == '\\' || e == '/')
          out->push_back(e);
        else if (e == 'n')
          out->push_back('\n');
        else if (e == 'r')
          out->push_back('\r');
        else if (e == 't')
          out->push_back('\t');
        else
          return Fail("unsupported escape");
      } else
        out->push_back(c);
    }
    if (pos_ == source_.size())
      return Fail("unterminated string");
    ++pos_;
    return true;
  }
  bool NumberOf(Value* out) {
    const char* begin = source_.c_str() + pos_;
    char* end = nullptr;
    double n = std::strtod(begin, &end);
    if (end == begin)
      return Fail("bad number");
    pos_ += static_cast<size_t>(end - begin);
    *out = n;
    return true;
  }
  bool ObjectOf(Value* out) {
    Object object;
    Consume('{');
    Skip();
    if (Consume('}')) {
      *out = std::move(object);
      return true;
    }
    for (;;) {
      std::string key;
      Value val;
      if (!String(&key) || !Consume(':') || !ValueOf(&val))
        return false;
      object.emplace(std::move(key), std::move(val));
      if (Consume('}'))
        break;
      if (!Consume(','))
        return Fail("expected comma");
    }
    *out = std::move(object);
    return true;
  }
  bool ArrayOf(Value* out) {
    Array array;
    Consume('[');
    Skip();
    if (Consume(']')) {
      *out = std::move(array);
      return true;
    }
    for (;;) {
      Value val;
      if (!ValueOf(&val))
        return false;
      array.push_back(std::move(val));
      if (Consume(']'))
        break;
      if (!Consume(','))
        return Fail("expected comma");
    }
    *out = std::move(array);
    return true;
  }
  const std::string& source_;
  std::string* error_;
  size_t pos_ = 0;
};
} // namespace
bool ParseFile(const std::string& path, Value* out, std::string* error) {
  std::ifstream file(path);
  if (!file) {
    *error = "cannot open JSON file: " + path;
    return false;
  }
  std::string source((std::istreambuf_iterator<char>(file)), {});
  return Parser(source, error).Parse(out);
}
const Value* FindPath(const Value& root, const char* path) {
  const Value* current = &root;
  std::istringstream parts(path);
  std::string key;
  while (std::getline(parts, key, '.')) {
    const auto* object = std::get_if<Object>(current);
    if (!object)
      return nullptr;
    auto it = object->find(key);
    if (it == object->end())
      return nullptr;
    current = &it->second;
  }
  return current;
}
bool Number(const Value* value, double* out) {
  if (const auto* n = value ? std::get_if<double>(value) : nullptr) {
    *out = *n;
    return true;
  }
  return false;
}
bool Boolean(const Value* value, bool* out) {
  if (const auto* b = value ? std::get_if<bool>(value) : nullptr) {
    *out = *b;
    return true;
  }
  return false;
}
} // namespace cvsdk::json
