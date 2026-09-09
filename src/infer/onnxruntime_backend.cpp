#include "infer/onnxruntime_backend.h"
#include "base/status.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

namespace cvsdk {
namespace {
std::vector<int> Nms(const std::vector<cv::Rect2f>& boxes, const std::vector<float>& scores,
                     float threshold) {
  std::vector<int> order(boxes.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });
  std::vector<int> keep;
  while (!order.empty()) {
    int i = order.front();
    keep.push_back(i);
    std::vector<int> next;
    for (size_t n = 1; n < order.size(); ++n) {
      int j = order[n];
      float inter = (boxes[i] & boxes[j]).area();
      float uni = boxes[i].area() + boxes[j].area() - inter;
      if (uni <= 0 || inter / uni <= threshold)
        next.push_back(j);
    }
    order = std::move(next);
  }
  return keep;
}
} // namespace
struct OnnxRuntimeBackend::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "cv_sdk"};
  Ort::SessionOptions options;
  std::unique_ptr<Ort::Session> session;
  std::string input_name, output_name;
  int input_w = 1280, input_h = 1280;
  float iou = .45F, threshold = .10F;
};
OnnxRuntimeBackend::OnnxRuntimeBackend() : impl_(std::make_unique<Impl>()) {
  impl_->options.SetIntraOpNumThreads(4);
  impl_->options.AddConfigEntry("session.intra_op.allow_spinning", "0");
  impl_->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
}
OnnxRuntimeBackend::~OnnxRuntimeBackend() = default;
CVSDK_Status OnnxRuntimeBackend::Load(const std::string& package) {
  try {
    const auto model = std::filesystem::path(package) / "artifacts/onnxruntime/model.onnx";
    if (!std::filesystem::is_regular_file(model)) {
      SetLastError("ONNX artifact not found: " + model.string());
      return CVSDK_NOT_FOUND;
    }
    impl_->session = std::make_unique<Ort::Session>(impl_->env, model.c_str(), impl_->options);
    Ort::AllocatorWithDefaultOptions allocator;
    auto name = impl_->session->GetInputNameAllocated(0, allocator);
    impl_->input_name = name.get();
    auto output = impl_->session->GetOutputNameAllocated(0, allocator);
    impl_->output_name = output.get();
    auto shape = impl_->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() == 4 && shape[2] > 0 && shape[3] > 0) {
      impl_->input_h = (int)shape[2];
      impl_->input_w = (int)shape[3];
    }
    return CVSDK_OK;
  } catch (const Ort::Exception& e) {
    SetLastError(std::string("ONNX load failed: ") + e.what());
    return CVSDK_INTERNAL_ERROR;
  }
}
CVSDK_Status OnnxRuntimeBackend::Run(const CVSDK_Image& image, std::vector<Detection>* output) {
  try {
    if (!impl_->session || !output || !image.data) {
      SetLastError("ONNX backend is not initialized");
      return CVSDK_INVALID_ARGUMENT;
    }
    cv::Mat in((int)image.height, (int)image.width, CV_8UC3, const_cast<uint8_t*>(image.data),
               image.stride_bytes),
        bgr = in;
    if (image.pixel_format == CVSDK_PIXEL_FORMAT_RGB8)
      cv::cvtColor(in, bgr, cv::COLOR_RGB2BGR);
    float ratio =
        std::min(impl_->input_w / (float)image.width, impl_->input_h / (float)image.height);
    int nw = std::lround(image.width * ratio), nh = std::lround(image.height * ratio),
        left = (impl_->input_w - nw) / 2, top = (impl_->input_h - nh) / 2;
    cv::Mat resized, padded, rgb;
    cv::resize(bgr, resized, {nw, nh}, 0, 0, cv::INTER_LINEAR);
    cv::copyMakeBorder(resized, padded, top, impl_->input_h - nh - top, left,
                       impl_->input_w - nw - left, cv::BORDER_CONSTANT, {114, 114, 114});
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    std::vector<float> tensor(3 * impl_->input_w * impl_->input_h);
    for (int y = 0; y < impl_->input_h; ++y)
      for (int x = 0; x < impl_->input_w; ++x) {
        auto p = rgb.at<cv::Vec3b>(y, x);
        for (int c = 0; c < 3; ++c)
          tensor[c * impl_->input_w * impl_->input_h + y * impl_->input_w + x] = p[c] / 255.F;
      }
    std::array<int64_t, 4> shape{1, 3, impl_->input_h, impl_->input_w};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto input = Ort::Value::CreateTensor<float>(memory, tensor.data(), tensor.size(), shape.data(),
                                                 shape.size());
    const char* names[] = {impl_->input_name.c_str()};
    const char* output_names[] = {impl_->output_name.c_str()};
    auto result = impl_->session->Run(Ort::RunOptions{nullptr}, names, &input, 1, output_names, 1);
    auto info = result[0].GetTensorTypeAndShapeInfo();
    auto dims = info.GetShape();
    const float* raw = result[0].GetTensorData<float>();
    if (dims.size() != 3) {
      SetLastError("unsupported YOLO output rank");
      return CVSDK_UNSUPPORTED;
    }
    size_t n = dims[2], channels = dims[1];
    bool cn = channels < dims[2];
    if (!cn) {
      n = dims[1];
      channels = dims[2];
    }
    std::vector<std::vector<cv::Rect2f>> boxes(channels - 4);
    std::vector<std::vector<float>> scores(channels - 4);
    for (size_t i = 0; i < n; ++i) {
      auto get = [&](size_t c) { return cn ? raw[c * n + i] : raw[i * channels + c]; };
      float best = 0;
      int cls = -1;
      for (size_t c = 4; c < channels; ++c)
        if (get(c) > best) {
          best = get(c);
          cls = (int)c - 4;
        }
      if (best < impl_->threshold || cls < 0)
        continue;
      float cx = get(0), cy = get(1), w = get(2), h = get(3);
      float x = (cx - w / 2 - left) / ratio, y = (cy - h / 2 - top) / ratio;
      boxes[cls].push_back({x, y, w / ratio, h / ratio});
      scores[cls].push_back(best);
    }
    output->clear();
    for (size_t cls = 0; cls < boxes.size(); ++cls)
      for (int k : Nms(boxes[cls], scores[cls], impl_->iou)) {
        auto r = boxes[cls][k];
        r.x = std::clamp(r.x, 0.F, (float)image.width);
        r.y = std::clamp(r.y, 0.F, (float)image.height);
        r.width = std::clamp(r.width, 0.F, (float)image.width - r.x);
        r.height = std::clamp(r.height, 0.F, (float)image.height - r.y);
        output->push_back({r.x, r.y, r.width, r.height, scores[cls][k], (int32_t)cls});
      }
    return CVSDK_OK;
  } catch (const Ort::Exception& e) {
    SetLastError(std::string("ONNX inference failed: ") + e.what());
    return CVSDK_INTERNAL_ERROR;
  }
}
} // namespace cvsdk
