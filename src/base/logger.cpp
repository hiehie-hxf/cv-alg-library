#include "base/logger.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>

#include "base/status.h"

namespace cvsdk {
namespace {
const char* LevelName(CVSDK_LogLevel level) {
  static const char* kNames[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"};
  return level >= CVSDK_LOG_TRACE && level <= CVSDK_LOG_OFF ? kNames[level] : "UNKNOWN";
}
std::string EscapeJson(const char* input) {
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(input ? input : ""); *p; ++p) {
    switch (*p) { case '\\': out += "\\\\"; break; case '"': out += "\\\""; break; case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break; default: if (*p < 0x20) out += "?"; else out += static_cast<char>(*p); }
  }
  return out;
}
std::string JsonLine(CVSDK_LogLevel level, const char* module, const char* message) {
  const auto now = std::chrono::system_clock::now(); const std::time_t raw = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &raw);
#else
  localtime_r(&raw, &local);
#endif
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::ostringstream out;
  out << "{\"ts\":\"" << std::put_time(&local, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms
      << "\",\"level\":\"" << LevelName(level) << "\",\"module\":\"" << EscapeJson(module)
      << "\",\"message\":\"" << EscapeJson(message) << "\",\"sdk_version\":\"0.1.0\"}";
  return out.str();
}
}

struct Logger::Impl {
  struct Entry { CVSDK_LogLevel level; std::string json; };
  struct RateState { std::chrono::steady_clock::time_point last; uint64_t suppressed = 0; };
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<Entry> queue;
  std::thread worker;
  bool stopping = false;
  CVSDK_LogLevel min_level = CVSDK_LOG_WARN;
  std::string file_path;
  uint64_t max_file_bytes = 0;
  uint32_t max_rotated_files = 0;
  uint32_t queue_capacity = 4096;
  CVSDK_LogCallback callback = nullptr;
  void* user_data = nullptr;
  uint64_t accepted = 0;
  uint64_t dropped = 0;
  std::unordered_map<std::string, RateState> rate_limits;
};

Logger& Logger::Instance() { static Logger logger; return logger; }
Logger::Logger() : impl_(new Impl) { impl_->worker = std::thread([this] {
  for (;;) {
    Impl::Entry entry;
    std::string path; uint64_t max_bytes; uint32_t max_files; CVSDK_LogCallback callback; void* user_data;
    { std::unique_lock<std::mutex> lock(impl_->mutex); impl_->cv.wait(lock, [this] { return impl_->stopping || !impl_->queue.empty(); });
      if (impl_->stopping && impl_->queue.empty()) return;
      entry = std::move(impl_->queue.front()); impl_->queue.pop_front(); path = impl_->file_path; max_bytes = impl_->max_file_bytes; max_files = impl_->max_rotated_files; callback = impl_->callback; user_data = impl_->user_data; }
    if (!path.empty()) {
      std::error_code error; const uint64_t current_size = std::filesystem::exists(path, error) ? std::filesystem::file_size(path, error) : 0;
      if (!error && max_bytes && current_size + entry.json.size() + 1 > max_bytes) {
        for (uint32_t i = max_files; i > 0; --i) { const std::string from = i == 1 ? path : path + "." + std::to_string(i - 1); const std::string to = path + "." + std::to_string(i); std::filesystem::rename(from, to, error); error.clear(); }
      }
      std::ofstream file(path, std::ios::app); if (file) file << entry.json << '\n';
    }
    if (callback) callback(entry.level, entry.json.c_str(), user_data);
    if (path.empty() && !callback) std::fprintf(stderr, "%s\n", entry.json.c_str());
  }
}); }
Logger::~Logger() { { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stopping = true; } impl_->cv.notify_all(); if (impl_->worker.joinable()) impl_->worker.join(); delete impl_; }

CVSDK_Status Logger::Configure(const CVSDK_LogOptions* options) {
  if (options && options->struct_size < sizeof(CVSDK_LogOptions)) { SetLastError("invalid CVSDK_LogOptions"); return CVSDK_INVALID_ARGUMENT; }
  if (options && (options->min_level < CVSDK_LOG_TRACE || options->min_level > CVSDK_LOG_OFF)) { SetLastError("invalid log level"); return CVSDK_INVALID_ARGUMENT; }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->min_level = options ? options->min_level : CVSDK_LOG_WARN;
  impl_->file_path = options && options->file_path ? options->file_path : "";
  impl_->max_file_bytes = options ? options->max_file_bytes : 0;
  impl_->max_rotated_files = options ? options->max_rotated_files : 0;
  impl_->queue_capacity = options && options->queue_capacity ? options->queue_capacity : 4096;
  impl_->callback = options ? options->callback : nullptr; impl_->user_data = options ? options->user_data : nullptr;
  return CVSDK_OK;
}
void Logger::Log(CVSDK_LogLevel level, const char* module, const char* message) {
  if (level < CVSDK_LOG_TRACE || level >= CVSDK_LOG_OFF || !module || !message) return;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (level < impl_->min_level) return;
  /* Repeated non-error entries are retained once per second; the next entry reports suppression. */
  const auto now = std::chrono::steady_clock::now();
  const std::string rate_key = std::to_string(level) + "\x1f" + module + "\x1f" + message;
  if (level <= CVSDK_LOG_WARN) {
    auto& rate = impl_->rate_limits[rate_key];
    if (rate.last.time_since_epoch().count() != 0 && now - rate.last < std::chrono::seconds(1)) { ++rate.suppressed; return; }
    if (rate.suppressed) {
      if (impl_->queue.size() < impl_->queue_capacity) {
        impl_->queue.push_back({level, JsonLine(level, module, ("previous identical messages suppressed=" + std::to_string(rate.suppressed)).c_str())});
        ++impl_->accepted;
      } else {
        ++impl_->dropped;
      }
      rate.suppressed = 0;
    }
    rate.last = now;
  }
  if (impl_->queue.size() >= impl_->queue_capacity) {
    /* Preserve the newest WARN/ERROR/FATAL where possible by evicting a lower priority record. */
    auto victim = impl_->queue.end();
    if (level >= CVSDK_LOG_WARN) for (auto it = impl_->queue.begin(); it != impl_->queue.end(); ++it) if (it->level < level) { victim = it; break; }
    if (victim != impl_->queue.end()) { impl_->queue.erase(victim); ++impl_->dropped; }
    else { ++impl_->dropped; return; }
  }
  impl_->queue.push_back({level, JsonLine(level, module, message)}); ++impl_->accepted; impl_->cv.notify_one();
}
CVSDK_Status Logger::GetStats(CVSDK_LogStats* stats) {
  if (!stats || stats->struct_size < sizeof(CVSDK_LogStats)) { SetLastError("invalid CVSDK_LogStats"); return CVSDK_INVALID_ARGUMENT; }
  std::lock_guard<std::mutex> lock(impl_->mutex); stats->accepted_count = impl_->accepted; stats->dropped_count = impl_->dropped; stats->queued_count = static_cast<uint32_t>(impl_->queue.size()); return CVSDK_OK;
}
}
