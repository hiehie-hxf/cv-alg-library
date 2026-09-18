/**
 * @file leak_vision_service.cpp
 * @brief 基于 CV SDK 的漏液分割 RTSP 推理服务。
 *
 * 服务进程通过公开 C ABI 调用 SDK，负责视频取流、断线重连、逐路推理、
 * 最新结果缓存以及面向平台层的 HTTP/JSON 接口。每路视频使用独立的
 * LeakProcessor（独立 ONNX 会话和独立滑动窗口），并配有独立的取流线程与
 * 推理线程，避免跨路共享时序状态。
 */

#include "cv_sdk/cv_sdk.h"

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

/**
 * @brief 处理进程终止信号。
 * @param signal_num 收到的信号编号。
 * @return 无返回值。
 */
void StopSignal(int signal_num) {
  (void)signal_num;
  g_running.store(false);
}

/**
 * @brief 对 JSON 字符串进行最小必要的转义。
 * @param value 待转义的 UTF-8 字符串。
 * @return 可直接放入 JSON 字符串值的文本。
 */
std::string JsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped.push_back(ch);
      break;
    }
  }
  return escaped;
}

/**
 * @brief 从 JSON 请求体中读取字符串字段。
 * @param body JSON 请求正文。
 * @param key 字段名。
 * @param required 是否要求字段存在且非空。
 * @param output 输出字符串。
 * @return 字段读取成功返回 true，否则返回 false。
 */
bool ReadJsonString(const std::string& body, const std::string& key, bool required,
                    std::string* output) {
  const std::string marker = "\"" + key + "\"";
  const size_t key_position = body.find(marker);
  if (key_position == std::string::npos)
    return !required;
  const size_t colon = body.find(':', key_position + marker.size());
  if (colon == std::string::npos)
    return false;
  size_t start = body.find('"', colon + 1);
  if (start == std::string::npos)
    return false;
  ++start;
  std::string value;
  bool escaped = false;
  for (size_t i = start; i < body.size(); ++i) {
    const char ch = body[i];
    if (escaped) {
      switch (ch) {
      case '"':
      case '\\':
      case '/':
        value.push_back(ch);
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        return false;
      }
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == '"') {
      if (required && value.empty())
        return false;
      *output = std::move(value);
      return true;
    } else {
      value.push_back(ch);
    }
  }
  return false;
}

/**
 * @brief 从 JSON 请求体中读取浮点字段。
 * @param body JSON 请求正文。
 * @param key 字段名。
 * @param output 输出浮点数。
 * @return 字段存在且格式正确返回 true，否则返回 false。
 */
bool ReadJsonNumber(const std::string& body, const std::string& key, double* output) {
  const std::string marker = "\"" + key + "\"";
  const size_t key_position = body.find(marker);
  if (key_position == std::string::npos)
    return false;
  const size_t colon = body.find(':', key_position + marker.size());
  if (colon == std::string::npos)
    return false;
  const char* begin = body.c_str() + colon + 1;
  while (*begin != '\0' && std::isspace(static_cast<unsigned char>(*begin)))
    ++begin;
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin)
    return false;
  *output = value;
  return true;
}

/**
 * @brief 返回漏液告警等级名称。
 * @param level SDK 告警等级。
 * @return 稳定的英文枚举名称。
 */
const char* AlertLevelName(CVSDK_LeakAlertLevel level) {
  switch (level) {
  case CVSDK_LEAK_ALERT_WARNING:
    return "warning";
  default:
    return "none";
  }
}

/**
 * @brief 将漏液目标数组序列化为 JSON。
 * @param items 漏液目标数组。
 * @return 漏液目标 JSON 数组。
 */
std::string LeakItemsJson(const std::vector<CVSDK_LeakItem>& items) {
  std::ostringstream json;
  json << '[';
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (i != 0)
      json << ',';
    json << std::fixed << std::setprecision(4) << "{\"x\":" << item.x << ",\"y\":" << item.y
         << ",\"width\":" << item.width << ",\"height\":" << item.height
         << ",\"score\":" << item.score << ",\"centroid_x\":" << item.centroid_x
         << ",\"centroid_y\":" << item.centroid_y << ",\"area\":" << item.area << '}';
  }
  json << ']';
  return json.str();
}

/**
 * @brief 描述一个视频流的最新处理结果。
 *
 * 所有字段由取流/推理线程写入，并由 HTTP 线程在互斥锁保护下读取。
 */
struct StreamSnapshot {
  uint64_t sequence = 0;
  uint64_t captured_frames = 0;
  uint64_t processed_frames = 0;
  double inference_ms = 0.0;
  CVSDK_LeakAlertState state{sizeof(CVSDK_LeakAlertState)};
  std::vector<CVSDK_LeakItem> items;
  std::string status = "created";
  std::string last_error;
  std::chrono::system_clock::time_point updated_at;
};

/**
 * @brief 取流线程与推理线程之间的最新帧交接槽。
 *
 * 取流线程直接覆盖旧帧，因此推理线程始终只处理最新一帧，不会积压。
 */
struct LatestFrame {
  std::mutex mutex;
  std::condition_variable ready;
  cv::Mat image;
  uint64_t sequence = 0;
  bool closed = false;
};

/**
 * @brief 单路 RTSP 视频处理会话。
 *
 * 输入：模型包、规则文件和 RTSP/摄像头地址；输出：线程安全的最新分割结果。
 * 会话析构时先停取流线程，再停推理线程，最后释放 SDK 句柄。
 */
class StreamSession {
public:
  /**
   * @brief 创建并启动一路视频会话。
   * @param id 平台侧业务流 ID。
   * @param url RTSP URL、摄像头序号或本地视频地址。
   * @param model_package SDK 模型包目录。
   * @param rules_json 漏液规则 JSON 文件路径。
   * @param error 输出初始化失败原因。
   * @return 成功返回会话指针，失败返回 nullptr。
   */
  static std::unique_ptr<StreamSession> Create(const std::string& id, const std::string& url,
                                               const std::string& model_package,
                                               const std::string& rules_json, std::string* error) {
    auto session = std::unique_ptr<StreamSession>(
        new StreamSession(id, url, model_package, rules_json));
    if (!session->Initialize(error))
      return nullptr;
    session->capture_thread_ = std::thread(&StreamSession::CaptureLoop, session.get());
    session->inference_thread_ = std::thread(&StreamSession::InferenceLoop, session.get());
    return session;
  }

  ~StreamSession() {
    stop_.store(true);
    {
      std::lock_guard<std::mutex> lock(latest_frame_.mutex);
      latest_frame_.closed = true;
    }
    latest_frame_.ready.notify_all();
    if (capture_thread_.joinable())
      capture_thread_.join();
    if (inference_thread_.joinable())
      inference_thread_.join();
    if (processor_)
      CVSDK_LeakProcessorDestroy(processor_);
  }

  StreamSession(const StreamSession&) = delete;
  StreamSession& operator=(const StreamSession&) = delete;

  /**
   * @brief 获取流 ID。
   * @return 流 ID 常量引用。
   */
  const std::string& Id() const {
    return id_;
  }

  /**
   * @brief 获取视频源地址。
   * @return 视频源地址常量引用。
   */
  const std::string& Url() const {
    return url_;
  }

  /**
   * @brief 获取当前快照。
   * @return 最新结果副本，调用方可安全持有。
   */
  StreamSnapshot Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
  }

private:
  StreamSession(std::string id, std::string url, std::string model_package, std::string rules_json)
      : id_(std::move(id)), url_(std::move(url)), model_package_(std::move(model_package)),
        rules_json_(std::move(rules_json)) {}

  /**
   * @brief 初始化 SDK 漏液处理器。
   * @param error 输出初始化失败原因。
   * @return 成功返回 true，否则返回 false。
   */
  bool Initialize(std::string* error) {
    CVSDK_LeakProcessorOptions options{sizeof(options), "onnxruntime", rules_json_.c_str(), 0.F, 0.F,
                                       {0}};
    const CVSDK_Status status =
        CVSDK_LeakProcessorCreate(model_package_.c_str(), &options, &processor_);
    if (status != CVSDK_OK) {
      *error = std::string("leak processor create failed: ") + CVSDK_GetLastError();
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot_.status = "starting";
      snapshot_.updated_at = std::chrono::system_clock::now();
    }
    return true;
  }

  /**
   * @brief 打开视频源，失败时按固定间隔重试。
   * @return 打开成功返回 true，收到停止请求返回 false。
   */
  bool OpenCapture(cv::VideoCapture* capture) {
    while (!stop_.load() && g_running.load()) {
      bool opened = false;
      const bool numeric_source =
          !url_.empty() && std::all_of(url_.begin(), url_.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
          });
      if (numeric_source) {
        try {
          opened = capture->open(std::stoi(url_), cv::CAP_ANY);
        } catch (const std::exception&) {
          SetStatus("error", "camera index is invalid");
          return false;
        }
      } else {
        opened = capture->open(url_, cv::CAP_ANY);
      }
      if (opened) {
        capture->set(cv::CAP_PROP_BUFFERSIZE, 1);
        return true;
      }
      SetStatus("reconnecting", "cannot open video source");
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return false;
  }

  /**
   * @brief 更新线程安全状态文本。
   * @param status 当前生命周期状态。
   * @param error 最近一次错误，可为空。
   * @return 无返回值。
   */
  void SetStatus(const std::string& status, const std::string& error = {}) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.status = status;
    snapshot_.last_error = error;
    snapshot_.updated_at = std::chrono::system_clock::now();
  }

  /**
   * @brief 取流线程：持续拉流并只保留最新一帧。
   * @return 无返回值，线程退出时释放视频句柄。
   */
  void CaptureLoop();

  /**
   * @brief 推理线程：消费最新帧并刷新快照。
   * @return 无返回值。
   */
  void InferenceLoop();

  const std::string id_;
  const std::string url_;
  const std::string model_package_;
  const std::string rules_json_;
  CVSDK_LeakProcessor* processor_ = nullptr;
  std::atomic<bool> stop_{false};
  std::thread capture_thread_;
  std::thread inference_thread_;
  LatestFrame latest_frame_;
  mutable std::mutex mutex_;
  StreamSnapshot snapshot_;
};

/**
 * @brief 取流线程：持续拉流并只保留最新一帧。
 * @return 无返回值，线程退出时释放视频句柄。
 */
void StreamSession::CaptureLoop() {
  cv::VideoCapture capture;
  cv::Mat frame;
  if (!OpenCapture(&capture)) {
    SetStatus("stopped");
    return;
  }
  SetStatus("running");
  uint64_t sequence = 0;
  while (!stop_.load() && g_running.load()) {
    if (!capture.read(frame) || frame.empty()) {
      capture.release();
      if (!OpenCapture(&capture))
        break;
      continue;
    }
    ++sequence;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot_.captured_frames++;
      snapshot_.updated_at = std::chrono::system_clock::now();
    }
    // 直接覆盖旧帧：推理慢于取流时只保留最新画面，避免处理积压的过期帧。
    {
      std::lock_guard<std::mutex> lock(latest_frame_.mutex);
      latest_frame_.image = frame.clone();
      latest_frame_.sequence = sequence;
    }
    latest_frame_.ready.notify_one();
  }
  capture.release();
  {
    std::lock_guard<std::mutex> lock(latest_frame_.mutex);
    latest_frame_.closed = true;
  }
  latest_frame_.ready.notify_all();
  SetStatus("stopped");
}

/**
 * @brief 推理线程：消费最新帧并刷新快照。
 * @return 无返回值。
 */
void StreamSession::InferenceLoop() {
  // CVSDK_LeakProcessorProcess 是有状态接口，每次调用都会推进滑动窗口，
  // 因此必须固定预分配容量、每帧只调用一次；容量不足时也不能再查询一次，
  // 否则时序窗口会被多推进一帧。
  CVSDK_LeakItem items[16];
  uint64_t consumed = 0;
  while (!stop_.load() && g_running.load()) {
    cv::Mat frame;
    uint64_t sequence = 0;
    {
      std::unique_lock<std::mutex> lock(latest_frame_.mutex);
      latest_frame_.ready.wait(lock, [&] {
        return !stop_.load() || !g_running.load() || latest_frame_.closed ||
               latest_frame_.sequence != consumed;
      });
      if (stop_.load() || !g_running.load() ||
          (latest_frame_.closed && latest_frame_.sequence == consumed))
        break;
      frame = latest_frame_.image;
      sequence = latest_frame_.sequence;
      consumed = sequence;
    }
    if (frame.empty())
      continue;
    const auto started = std::chrono::steady_clock::now();
    CVSDK_Image image{sizeof(CVSDK_Image), frame.data, static_cast<uint32_t>(frame.cols),
                      static_cast<uint32_t>(frame.rows), static_cast<uint32_t>(frame.step),
                      CVSDK_PIXEL_FORMAT_BGR8};
    CVSDK_LeakItemList list{sizeof(list), items, 16, 0};
    CVSDK_LeakAlertState state{sizeof(state)};
    const CVSDK_Status status = CVSDK_LeakProcessorProcess(processor_, &image, &list, &state);
    uint32_t item_count = 0;
    std::string warning;
    if (status == CVSDK_OK) {
      item_count = list.count;
    } else if (status == CVSDK_BUFFER_TOO_SMALL) {
      // 目标数超过预分配容量时 SDK 不会写入任何明细，只能丢弃本帧明细；
      // 告警状态在容量校验之前已经填好，因此仍然可用。
      warning = "leak count exceeds preallocated buffer";
    } else {
      SetStatus("error", CVSDK_GetLastError());
      continue;
    }
    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot_.sequence = sequence;
      snapshot_.processed_frames++;
      snapshot_.inference_ms = elapsed;
      snapshot_.state = state;
      snapshot_.items.assign(items, items + item_count);
      snapshot_.status = "running";
      snapshot_.last_error = warning;
      snapshot_.updated_at = std::chrono::system_clock::now();
    }
  }
}

/**
 * @brief RTSP 推理服务及 HTTP 路由管理器。
 * @param listen_address 监听地址，通常为 0.0.0.0。
 * @param port HTTP 监听端口。
 * @param default_model 默认模型包目录。
 * @param default_rules 默认漏液规则 JSON 文件。
 */
class LeakVisionService {
public:
  LeakVisionService(std::string listen_address, int port, std::string default_model,
                    std::string default_rules)
      : listen_address_(std::move(listen_address)), port_(port),
        default_model_(std::move(default_model)), default_rules_(std::move(default_rules)) {}

  /**
   * @brief 启动 HTTP 服务并进入事件循环。
   * @return 进程退出状态码。
   */
  int Run() {
    event_base* base = event_base_new();
    if (!base) {
      std::cerr << "event_base_new failed\n";
      return 1;
    }
    evhttp* http = evhttp_new(base);
    if (!http) {
      event_base_free(base);
      std::cerr << "evhttp_new failed\n";
      return 1;
    }
    evhttp_set_gencb(http, &LeakVisionService::HandleRequest, this);
    if (evhttp_bind_socket(http, listen_address_.c_str(), port_) != 0) {
      evhttp_free(http);
      event_base_free(base);
      std::cerr << "cannot bind " << listen_address_ << ':' << port_ << '\n';
      return 2;
    }
    base_ = base;
    http_ = http;
    std::cout << "leak vision service listening on http://" << listen_address_ << ':' << port_
              << '\n';
    while (g_running.load()) {
      event_base_loop(base_, EVLOOP_ONCE);
    }
    event_base_loopbreak(base_);
    streams_.clear();
    evhttp_free(http_);
    event_base_free(base_);
    http_ = nullptr;
    base_ = nullptr;
    return 0;
  }

private:
  /**
   * @brief HTTP 请求统一入口。
   * @param request libevent HTTP 请求对象。
   * @param context LeakVisionService 实例指针。
   * @return 无返回值，响应通过 libevent 写回客户端。
   */
  static void HandleRequest(evhttp_request* request, void* context) {
    static_cast<LeakVisionService*>(context)->Handle(request);
  }

  /**
   * @brief 生成 JSON HTTP 响应。
   * @param request 当前 HTTP 请求。
   * @param code HTTP 状态码。
   * @param body JSON 正文。
   * @return 无返回值。
   */
  static void ReplyJson(evhttp_request* request, int code, const std::string& body) {
    evbuffer* output = evbuffer_new();
    if (!output)
      return;
    evbuffer_add(output, body.data(), body.size());
    evhttp_add_header(evhttp_request_get_output_headers(request), "Content-Type",
                      "application/json; charset=utf-8");
    evhttp_add_header(evhttp_request_get_output_headers(request), "Cache-Control", "no-store");
    evhttp_send_reply(request, code, code == 200 ? "OK" : "Error", output);
    evbuffer_free(output);
  }

  /**
   * @brief 解析路径并路由到平台接口。
   * @param request 当前 HTTP 请求。
   * @return 无返回值。
   */
  void Handle(evhttp_request* request) {
    const std::string uri = evhttp_request_get_uri(request);
    const auto query_position = uri.find('?');
    const std::string path = uri.substr(0, query_position);
    const char* method = evhttp_request_get_command(request) == EVHTTP_REQ_GET      ? "GET"
                         : evhttp_request_get_command(request) == EVHTTP_REQ_POST   ? "POST"
                         : evhttp_request_get_command(request) == EVHTTP_REQ_DELETE ? "DELETE"
                                                                                    : "OTHER";
    if (path == "/health" && std::string(method) == "GET") {
      size_t stream_count = 0;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_count = streams_.size();
      }
      ReplyJson(request, 200,
                "{\"status\":\"ok\",\"streams\":" + std::to_string(stream_count) + "}");
      return;
    }
    if (path == "/v1/streams" && std::string(method) == "GET") {
      ReplyJson(request, 200, ListStreamsJson());
      return;
    }
    if (path == "/v1/streams" && std::string(method) == "POST") {
      CreateStream(request);
      return;
    }
    constexpr const char* prefix = "/v1/streams/";
    if (path.rfind(prefix, 0) == 0) {
      const std::string id = path.substr(std::char_traits<char>::length(prefix));
      if (id.empty() || id.find('/') != std::string::npos) {
        ReplyJson(request, 404, "{\"error\":\"route not found\"}");
        return;
      }
      if (std::string(method) == "GET") {
        GetResult(request, id);
        return;
      }
      if (std::string(method) == "DELETE") {
        DeleteStream(request, id);
        return;
      }
    }
    ReplyJson(request, 404, "{\"error\":\"route not found\"}");
  }

  /**
   * @brief 创建一路 RTSP 漏液推理流。
   * @param request 包含 id、url 和可选 model_package/rules_json 的请求。
   * @return 无返回值。
   */
  void CreateStream(evhttp_request* request) {
    auto* input = evhttp_request_get_input_buffer(request);
    const size_t length = evbuffer_get_length(input);
    std::string body(length, '\0');
    evbuffer_copyout(input, body.data(), length);
    std::string id;
    std::string url;
    std::string model_package;
    std::string rules_json;
    if (!ReadJsonString(body, "id", true, &id) || !ReadJsonString(body, "url", true, &url) ||
        !ReadJsonString(body, "model_package", false, &model_package) ||
        !ReadJsonString(body, "rules_json", false, &rules_json)) {
      ReplyJson(request, 400,
                "{\"error\":\"id and url are required; model_package/rules_json may use defaults\"}");
      return;
    }
    if (model_package.empty())
      model_package = default_model_;
    if (rules_json.empty())
      rules_json = default_rules_;
    std::lock_guard<std::mutex> lock(mutex_);
    if (streams_.find(id) != streams_.end()) {
      ReplyJson(request, 409, "{\"error\":\"stream already exists\"}");
      return;
    }
    std::string error;
    auto session = StreamSession::Create(id, url, model_package, rules_json, &error);
    if (!session) {
      ReplyJson(request, 400, "{\"error\":\"" + JsonEscape(error) + "\"}");
      return;
    }
    streams_.emplace(id, std::move(session));
    ReplyJson(request, 201, "{\"id\":\"" + JsonEscape(id) + "\",\"status\":\"starting\"}");
  }

  /**
   * @brief 停止并删除一路视频流。
   * @param request HTTP 请求对象。
   * @param id 平台侧流 ID。
   * @return 无返回值。
   */
  void DeleteStream(evhttp_request* request, const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = streams_.find(id);
    if (it == streams_.end()) {
      ReplyJson(request, 404, "{\"error\":\"stream not found\"}");
      return;
    }
    streams_.erase(it);
    ReplyJson(request, 200, "{\"id\":\"" + JsonEscape(id) + "\",\"status\":\"stopped\"}");
  }

  /**
   * @brief 返回一路视频流的状态与最近告警。
   * @param request HTTP 请求对象。
   * @param id 平台侧流 ID。
   * @return 无返回值。
   */
  void GetResult(evhttp_request* request, const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = streams_.find(id);
    if (it == streams_.end()) {
      ReplyJson(request, 404, "{\"error\":\"stream not found\"}");
      return;
    }
    const StreamSnapshot snapshot = it->second->Snapshot();
    std::ostringstream body;
    body << std::fixed << std::setprecision(3) << "{\"id\":\"" << JsonEscape(id) << "\",\"url\":\""
         << JsonEscape(it->second->Url()) << "\",\"status\":\"" << JsonEscape(snapshot.status)
         << "\",\"sequence\":" << snapshot.sequence
         << ",\"captured_frames\":" << snapshot.captured_frames
         << ",\"processed_frames\":" << snapshot.processed_frames
         << ",\"inference_ms\":" << snapshot.inference_ms << ",\"alert\":{\"level\":\""
         << AlertLevelName(snapshot.state.level)
         << "\",\"leak_count\":" << snapshot.state.leak_count
         << ",\"hits\":" << snapshot.state.hits
         << ",\"window_filled\":" << snapshot.state.window_filled
         << ",\"total_area\":" << snapshot.state.total_area
         << ",\"centroid_y\":" << snapshot.state.centroid_y
         << ",\"max_confidence\":" << snapshot.state.max_confidence
         << ",\"area_growing\":" << snapshot.state.area_growing
         << ",\"centroid_down\":" << snapshot.state.centroid_down << ",\"reason\":\""
         << JsonEscape(snapshot.state.reason) << "\"},\"leaks\":" << LeakItemsJson(snapshot.items);
    if (!snapshot.last_error.empty())
      body << ",\"last_error\":\"" << JsonEscape(snapshot.last_error) << '"';
    body << '}';
    ReplyJson(request, 200, body.str());
  }

  /**
   * @brief 返回所有视频流的状态摘要。
   * @return JSON 对象。
   */
  std::string ListStreamsJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream body;
    body << "{\"items\":[";
    size_t index = 0;
    for (const auto& entry : streams_) {
      if (index++ != 0)
        body << ',';
      const auto snapshot = entry.second->Snapshot();
      body << "{\"id\":\"" << JsonEscape(entry.first) << "\",\"status\":\""
           << JsonEscape(snapshot.status) << "\",\"sequence\":" << snapshot.sequence
           << ",\"leak_count\":" << snapshot.items.size() << '}';
    }
    body << "]}";
    return body.str();
  }

  std::string listen_address_;
  int port_ = 8080;
  std::string default_model_;
  std::string default_rules_;
  event_base* base_ = nullptr;
  evhttp* http_ = nullptr;
  mutable std::mutex mutex_;
  std::map<std::string, std::unique_ptr<StreamSession>> streams_;
};

/**
 * @brief 解析命令行参数并启动服务。
 * @param argc 参数数量。
 * @param argv 参数数组。
 * @return 进程退出状态码。
 */
int Main(int argc, char** argv) {
  std::string listen_address = "0.0.0.0";
  int port = 8080;
  std::string model_package = "models/leak_seg_1280";
  std::string rules = "models/leak_seg_1280/leak_rules.json";
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    auto next = [&](std::string* value) {
      if (i + 1 >= argc)
        return false;
      *value = argv[++i];
      return true;
    };
    std::string value;
    if (argument == "--listen" && next(&listen_address)) {
      continue;
    } else if (argument == "--port" && next(&value)) {
      port = std::stoi(value);
    } else if (argument == "--model" && next(&model_package)) {
      continue;
    } else if (argument == "--rules" && next(&rules)) {
      continue;
    } else if (argument == "--help") {
      std::cout << "usage: " << argv[0]
                << " [--listen 0.0.0.0] [--port 8080] [--model package] [--rules rules.json]\n";
      return 0;
    } else {
      std::cerr << "invalid argument: " << argument << '\n';
      return 2;
    }
  }
  if (port <= 0 || port > 65535) {
    std::cerr << "port must be in [1,65535]\n";
    return 2;
  }
  std::signal(SIGINT, StopSignal);
  std::signal(SIGTERM, StopSignal);
  LeakVisionService service(std::move(listen_address), port, std::move(model_package),
                            std::move(rules));
  return service.Run();
}

} // namespace

int main(int argc, char** argv) {
  return Main(argc, argv);
}
