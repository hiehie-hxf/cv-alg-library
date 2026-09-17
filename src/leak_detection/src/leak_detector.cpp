#include "leak_detector.h"

#include "leak_filter.h"
#include "postprocess.h"
#include "preprocess.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#ifndef LEAK_WITH_ONNXRUNTIME
#define LEAK_WITH_ONNXRUNTIME 0
#endif

#if LEAK_WITH_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace leak {
namespace {

// ---------- 极简 JSON 取值工具 ----------
// 够用就好：按 "key" 定位，再解析紧随其后的数字或 [n, n]。
// 注意：不做完整 JSON 语法校验，键名重复时会命中第一个。

bool ReadFileToString(const std::string& path, std::string* out) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    return false;
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  *out = oss.str();
  return true;
}

// 返回 "key" 之后、冒号之后第一个非空白字符的位置；找不到返回 nullptr。
const char* FindValue(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  std::size_t pos = json.find(needle);
  if (pos == std::string::npos) {
    return nullptr;
  }
  pos += needle.size();
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] != ':') {
    return nullptr;
  }
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  return pos < json.size() ? json.c_str() + pos : nullptr;
}

bool ParseNumber(const char* p, double* out) {
  if (p == nullptr) {
    return false;
  }
  char* end = nullptr;
  const double v = std::strtod(p, &end);
  if (end == p) {
    return false;
  }
  *out = v;
  return true;
}

// 解析 [a, b]，用于 model.input_size
bool ParseIntPair(const char* p, int* a, int* b) {
  if (p == nullptr || *p != '[') {
    return false;
  }
  const char* cur = p + 1;
  char* end = nullptr;
  const long v1 = std::strtol(cur, &end, 10);
  if (end == cur) {
    return false;
  }
  cur = end;
  while (*cur == ' ' || *cur == ',' || *cur == '\t') {
    ++cur;
  }
  const long v2 = std::strtol(cur, &end, 10);
  if (end == cur) {
    return false;
  }
  *a = static_cast<int>(v1);
  *b = static_cast<int>(v2);
  return true;
}

}  // namespace

struct LeakDetector::Impl {
  LeakConfig config;
  bool model_loaded = false;

#if LEAK_WITH_ONNXRUNTIME
  std::unique_ptr<Ort::Env> env;
  std::unique_ptr<Ort::Session> session;
  std::string input_name;
  std::vector<std::string> output_names;
#endif

  std::unique_ptr<LeakFilter> filter;
};

LeakDetector::LeakDetector() : impl_(new Impl()) {}

LeakDetector::~LeakDetector() = default;

bool LeakDetector::LoadModel(const std::string& onnx_path) {
#if !LEAK_WITH_ONNXRUNTIME
  (void)onnx_path;
  std::cerr << "leak_detector: 编译时未启用 ONNX Runtime（stub 模式）\n";
  return false;
#else
  try {
    impl_->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "leak_detection");

    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(1);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    impl_->session = std::make_unique<Ort::Session>(*impl_->env, onnx_path.c_str(), options);

    Ort::AllocatorWithDefaultOptions allocator;

    // 输入名
    {
      auto name = impl_->session->GetInputNameAllocated(0, allocator);
      impl_->input_name = name.get();
    }

    // 输出名
    const std::size_t output_count = impl_->session->GetOutputCount();
    impl_->output_names.clear();
    for (std::size_t i = 0; i < output_count; ++i) {
      auto name = impl_->session->GetOutputNameAllocated(i, allocator);
      impl_->output_names.emplace_back(name.get());
    }
    if (impl_->output_names.size() < 2) {
      std::cerr << "leak_detector: 期望 2 个输出，实际 " << impl_->output_names.size() << "\n";
      return false;
    }

    // 输入 shape，形如 [1, 3, H, W]
    const std::vector<int64_t> in_shape =
        impl_->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (in_shape.size() == 4 && in_shape[2] > 0 && in_shape[3] > 0) {
      impl_->config.input_h = static_cast<int>(in_shape[2]);
      impl_->config.input_w = static_cast<int>(in_shape[3]);
    }

    std::cout << "leak_detector: 模型已加载 input=" << impl_->input_name
              << " size=" << impl_->config.input_w << "x" << impl_->config.input_h
              << " outputs=" << impl_->output_names.size() << "\n";

    impl_->filter = std::make_unique<LeakFilter>(impl_->config);
    impl_->model_loaded = true;
    return true;
  } catch (const Ort::Exception& e) {
    std::cerr << "leak_detector: 加载 ONNX 失败: " << e.what() << "\n";
    impl_->model_loaded = false;
    return false;
  } catch (const std::exception& e) {
    std::cerr << "leak_detector: 加载模型异常: " << e.what() << "\n";
    impl_->model_loaded = false;
    return false;
  }
#endif
}

bool LeakDetector::LoadConfig(const std::string& json_path) {
  std::string json;
  if (!ReadFileToString(json_path, &json)) {
    std::cerr << "leak_detector: 无法读取配置 " << json_path << "\n";
    return false;
  }

  // 以当前值为默认，缺失字段保留默认
  LeakConfig cfg = impl_->config;
  double d = 0.0;
  int a = 0;
  int b = 0;

  if (ParseIntPair(FindValue(json, "input_size"), &a, &b)) {
    cfg.input_w = a;
    cfg.input_h = b;
  }
  if (ParseNumber(FindValue(json, "candidate_conf"), &d)) {
    cfg.candidate_conf = static_cast<float>(d);
  }
  if (ParseNumber(FindValue(json, "iou_threshold"), &d)) {
    cfg.iou_threshold = static_cast<float>(d);
  }
  if (ParseNumber(FindValue(json, "mask_threshold"), &d)) {
    cfg.mask_threshold = static_cast<float>(d);
  }
  if (ParseNumber(FindValue(json, "window"), &d)) {
    cfg.window = static_cast<int>(d);
  }
  if (ParseNumber(FindValue(json, "min_hits"), &d)) {
    cfg.min_hits = static_cast<int>(d);
  }
  if (ParseNumber(FindValue(json, "area_growth_ratio"), &d)) {
    cfg.area_growth_ratio = static_cast<float>(d);
  }
  if (ParseNumber(FindValue(json, "centroid_down_px"), &d)) {
    cfg.centroid_down_px = static_cast<float>(d);
  }

  impl_->config = cfg;

  if (impl_->filter) {
    impl_->filter->Configure(impl_->config);
  } else {
    impl_->filter = std::make_unique<LeakFilter>(impl_->config);
  }

  std::cout << "leak_detector: 配置已加载 input=" << cfg.input_w << "x" << cfg.input_h
            << " conf=" << cfg.candidate_conf << " iou=" << cfg.iou_threshold
            << " mask=" << cfg.mask_threshold << " window=" << cfg.window
            << " min_hits=" << cfg.min_hits << "\n";
  return true;
}

bool LeakDetector::Infer(const cv::Mat& bgr,
                         std::vector<LeakItem>* items,
                         LeakAlertState* state) {
  if (items == nullptr || state == nullptr) {
    return false;
  }
  if (bgr.empty()) {
    return false;
  }

#if !LEAK_WITH_ONNXRUNTIME
  (void)bgr;
  items->clear();
  *state = LeakAlertState();
  std::cerr << "leak_detector: stub 模式，未执行推理\n";
  return false;
#else
  if (!impl_->model_loaded || !impl_->session) {
    return false;
  }

  try {
    // ---- 1. 前处理：letterbox -> RGB -> /255 -> HWC 转 CHW ----
    const LetterboxResult lb = Letterbox(bgr, impl_->config.input_w, impl_->config.input_h);
    if (lb.image.empty()) {
      return false;
    }

    cv::Mat rgb;
    cv::cvtColor(lb.image, rgb, cv::COLOR_BGR2RGB);

    cv::Mat blob;
    rgb.convertTo(blob, CV_32FC3, 1.0 / 255.0);

    const int h = blob.rows;
    const int w = blob.cols;
    const std::size_t plane = static_cast<std::size_t>(h) * static_cast<std::size_t>(w);

    std::vector<float> input_values(3 * plane);
    std::vector<cv::Mat> channels;
    cv::split(blob, channels);
    for (int c = 0; c < 3; ++c) {
      cv::Mat ch = channels[static_cast<std::size_t>(c)];
      if (!ch.isContinuous()) {
        ch = ch.clone();
      }
      std::memcpy(input_values.data() + static_cast<std::size_t>(c) * plane,
                  ch.ptr<float>(), plane * sizeof(float));
    }

    const std::array<int64_t, 4> input_shape = {1, 3, h, w};

    Ort::MemoryInfo mem_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        mem_info, input_values.data(), input_values.size(),
        input_shape.data(), input_shape.size());

    const char* input_names[] = {impl_->input_name.c_str()};
    std::vector<const char*> output_names;
    output_names.reserve(impl_->output_names.size());
    for (const std::string& n : impl_->output_names) {
      output_names.push_back(n.c_str());
    }

    // ---- 2. 推理 ----
    std::vector<Ort::Value> outputs = impl_->session->Run(
        Ort::RunOptions{nullptr}, input_names, &input_tensor, 1,
        output_names.data(), output_names.size());
    if (outputs.size() < 2) {
      std::cerr << "leak_detector: 推理输出不足 2 个\n";
      return false;
    }

    const std::vector<int64_t> shape0 = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    const std::vector<int64_t> shape1 = outputs[1].GetTensorTypeAndShapeInfo().GetShape();
    const float* data0 = outputs[0].GetTensorData<float>();
    const float* data1 = outputs[1].GetTensorData<float>();

    // ---- 3. 后处理 ----
    *items = PostprocessYoloSeg(data0, shape0.data(), data1, shape1.data(),
                                bgr.size(), lb.ratio, lb.pad_x, lb.pad_y,
                                impl_->config.candidate_conf,
                                impl_->config.iou_threshold,
                                impl_->config.mask_threshold);

    // ---- 4. 时序判定 ----
    *state = impl_->filter ? impl_->filter->Process(*items) : LeakAlertState();
    return true;
  } catch (const Ort::Exception& e) {
    std::cerr << "leak_detector: 推理失败: " << e.what() << "\n";
    items->clear();
    *state = LeakAlertState();
    return false;
  } catch (const cv::Exception& e) {
    std::cerr << "leak_detector: OpenCV 异常: " << e.what() << "\n";
    items->clear();
    *state = LeakAlertState();
    return false;
  }
#endif
}

void LeakDetector::Reset() {
  // 清空时序累积状态；模型保持已加载。
  if (impl_->filter) {
    impl_->filter->Reset();
  }
}

}  // namespace leak
