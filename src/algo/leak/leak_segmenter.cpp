#include "algo/leak/leak_segmenter.h"

#include "base/status.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

// 本文件只负责图像张量预处理、ONNX Runtime 调用和 YOLOv8-Seg 输出解码；
// 面积与质心的多帧确认由 leak_filter 任务层负责，二者可独立回归测试。

namespace cvsdk {
namespace {
/** 输入：分类器原始输出；输出：sigmoid 激活值。 */
float Sigmoid(float value) {
  return 1.F / (1.F + std::exp(-value));
}

/** 输入：数值与上下界；输出：限制到区间内的整数。 */
int ClampInt(int value, int low, int high) {
  return std::max(low, std::min(value, high));
}

/** 输入：两个矩形；输出：交并比。 */
float IoU(const cv::Rect2f& lhs, const cv::Rect2f& rhs) {
  const float left = std::max(lhs.x, rhs.x);
  const float top = std::max(lhs.y, rhs.y);
  const float right = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const float bottom = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  const float intersection = std::max(0.F, right - left) * std::max(0.F, bottom - top);
  const float union_area = lhs.width * lhs.height + rhs.width * rhs.height - intersection;
  return union_area > 0.F ? intersection / union_area : 0.F;
}

/** 输入：矩形和置信度数组、IoU 阈值；输出：按置信度降序贪心保留的下标。 */
std::vector<int> Nms(const std::vector<cv::Rect2f>& boxes, const std::vector<float>& scores,
                     float threshold) {
  std::vector<int> order(boxes.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });
  std::vector<int> keep;
  while (!order.empty()) {
    const int current = order.front();
    keep.push_back(current);
    std::vector<int> next;
    for (size_t i = 1; i < order.size(); ++i)
      if (IoU(boxes[current], boxes[order[i]]) <= threshold)
        next.push_back(order[i]);
    order = std::move(next);
  }
  return keep;
}

/** letterbox 结果：等比缩放后的画布、缩放比和两侧填充像素。 */
struct LetterboxResult {
  cv::Mat image;
  float ratio = 1.F;
  int pad_x = 0, pad_y = 0;
};

/** 输入：源图与网络输入尺寸；输出：填充值为 114 的 letterbox 画布。 */
LetterboxResult LetterboxImage(const cv::Mat& source, int input_w, int input_h) {
  LetterboxResult result;
  result.ratio = std::min(input_w / static_cast<float>(source.cols),
                          input_h / static_cast<float>(source.rows));
  const int width = static_cast<int>(std::lround(source.cols * result.ratio));
  const int height = static_cast<int>(std::lround(source.rows * result.ratio));
  result.pad_x = (input_w - width) / 2;
  result.pad_y = (input_h - height) / 2;
  cv::Mat resized;
  cv::resize(source, resized, {width, height}, 0, 0, cv::INTER_LINEAR);
  cv::copyMakeBorder(resized, result.image, result.pad_y, input_h - height - result.pad_y,
                     result.pad_x, input_w - width - result.pad_x, cv::BORDER_CONSTANT,
                     {114, 114, 114});
  return result;
}
} // namespace

struct LeakSegmenter::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "cv_sdk_leak"};
  Ort::SessionOptions options;
  std::unique_ptr<Ort::Session> session;
  std::string input_name, predictions_name, prototypes_name;
  int input_w = 1280, input_h = 1280;
  LeakConfig config;
};

LeakSegmenter::LeakSegmenter() = default;
LeakSegmenter::~LeakSegmenter() = default;

CVSDK_Status LeakSegmenter::Init(const char* package_dir, const LeakConfig& config,
                                 const char* backend) {
  if (!package_dir) {
    SetLastError("leak model package is required");
    return CVSDK_INVALID_ARGUMENT;
  }
  const char* selected = backend ? backend : "onnxruntime";
  if (std::strcmp(selected, "onnxruntime") && std::strcmp(selected, "onnx") &&
      std::strcmp(selected, "onnxruntime-cuda") && std::strcmp(selected, "onnx-cuda")) {
    SetLastError("unsupported leak segmenter backend");
    return CVSDK_UNSUPPORTED;
  }
#ifndef CVSDK_WITH_ONNXRUNTIME_CUDA
  if (!std::strcmp(selected, "onnxruntime-cuda") || !std::strcmp(selected, "onnx-cuda")) {
    SetLastError("ONNX Runtime CUDA provider is not enabled in this build");
    return CVSDK_UNSUPPORTED;
  }
#endif
  try {
    impl_ = std::make_unique<Impl>();
    impl_->config = config;
    impl_->options.SetIntraOpNumThreads(4);
    impl_->options.AddConfigEntry("session.intra_op.allow_spinning", "0");
    impl_->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef CVSDK_WITH_ONNXRUNTIME_CUDA
    if (!std::strcmp(selected, "onnxruntime-cuda") || !std::strcmp(selected, "onnx-cuda")) {
      OrtCUDAProviderOptions cuda_options{};
      cuda_options.device_id = 0;
      impl_->options.AppendExecutionProvider_CUDA(cuda_options);
    }
#endif
    const auto model = std::filesystem::path(package_dir) / "artifacts/onnxruntime/model.onnx";
    if (!std::filesystem::is_regular_file(model)) {
      SetLastError("leak model package requires artifacts/onnxruntime/model.onnx");
      return CVSDK_NOT_FOUND;
    }
    impl_->session = std::make_unique<Ort::Session>(impl_->env, model.c_str(), impl_->options);
    // 分割任务必须同时拿到掩码系数和原型，缺少任一都无法还原实例掩码。
    if (impl_->session->GetOutputCount() < 2) {
      SetLastError("leak model must expose two outputs (predictions and prototypes)");
      return CVSDK_UNSUPPORTED;
    }
    Ort::AllocatorWithDefaultOptions allocator;
    impl_->input_name = impl_->session->GetInputNameAllocated(0, allocator).get();
    impl_->predictions_name = impl_->session->GetOutputNameAllocated(0, allocator).get();
    impl_->prototypes_name = impl_->session->GetOutputNameAllocated(1, allocator).get();
    const auto shape = impl_->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4 || shape[2] <= 0 || shape[3] <= 0) {
      SetLastError("leak model input must be a fixed NCHW tensor");
      return CVSDK_UNSUPPORTED;
    }
    impl_->input_h = static_cast<int>(shape[2]);
    impl_->input_w = static_cast<int>(shape[3]);
    return CVSDK_OK;
  } catch (const Ort::Exception& e) {
    SetLastError(std::string("leak model load failed: ") + e.what());
    return CVSDK_INTERNAL_ERROR;
  }
}

CVSDK_Status LeakSegmenter::Run(const CVSDK_Image& image, std::vector<LeakItem>* output) {
  if (!impl_ || !output || !image.data || image.width == 0 || image.height == 0) {
    SetLastError("leak segmenter is not initialized");
    return CVSDK_INVALID_ARGUMENT;
  }
  try {
    cv::Mat source(static_cast<int>(image.height), static_cast<int>(image.width), CV_8UC3,
                   const_cast<uint8_t*>(image.data), image.stride_bytes), bgr;
    if (image.pixel_format == CVSDK_PIXEL_FORMAT_RGB8)
      cv::cvtColor(source, bgr, cv::COLOR_RGB2BGR);
    else
      bgr = source;

    // 固定输入尺寸加 letterbox，保持与训练/导出侧的预处理契约一致。
    const LetterboxResult letterbox = LetterboxImage(bgr, impl_->input_w, impl_->input_h);
    cv::Mat rgb;
    cv::cvtColor(letterbox.image, rgb, cv::COLOR_BGR2RGB);

    const int plane = impl_->input_w * impl_->input_h;
    std::vector<float> tensor(3 * plane);
    for (int y = 0; y < impl_->input_h; ++y)
      for (int x = 0; x < impl_->input_w; ++x) {
        const auto pixel = rgb.at<cv::Vec3b>(y, x);
        for (int c = 0; c < 3; ++c)
          tensor[c * plane + y * impl_->input_w + x] = pixel[c] / 255.F;
      }

    std::array<int64_t, 4> shape{1, 3, impl_->input_h, impl_->input_w};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto value = Ort::Value::CreateTensor<float>(memory, tensor.data(), tensor.size(), shape.data(),
                                                 shape.size());
    const char* inputs[] = {impl_->input_name.c_str()};
    const char* outputs[] = {impl_->predictions_name.c_str(), impl_->prototypes_name.c_str()};
    auto result = impl_->session->Run(Ort::RunOptions{nullptr}, inputs, &value, 1, outputs, 2);

    // YOLOv8-Seg 输出布局为 [batch, dims, num_preds]，dims 在前、候选在后。
    const auto predictions_shape = result[0].GetTensorTypeAndShapeInfo().GetShape();
    const auto prototypes_shape = result[1].GetTensorTypeAndShapeInfo().GetShape();
    if (predictions_shape.size() != 3 || prototypes_shape.size() != 4) {
      SetLastError("unsupported leak model output rank");
      return CVSDK_UNSUPPORTED;
    }
    const float* predictions = result[0].GetTensorData<float>();
    const float* prototypes = result[1].GetTensorData<float>();
    const int num_dims = static_cast<int>(predictions_shape[1]);
    const int num_preds = static_cast<int>(predictions_shape[2]);
    const int proto_count = static_cast<int>(prototypes_shape[1]);
    const int proto_h = static_cast<int>(prototypes_shape[2]);
    const int proto_w = static_cast<int>(prototypes_shape[3]);
    const int num_masks = num_dims - 5; // 4 个框分量加 1 个单类置信度
    if (num_preds <= 0 || num_masks <= 0 || proto_count < num_masks || proto_h <= 0 ||
        proto_w <= 0) {
      SetLastError("unsupported leak model output layout");
      return CVSDK_UNSUPPORTED;
    }

    const float image_area = static_cast<float>(image.width) * static_cast<float>(image.height);
    const float pad_x = static_cast<float>(letterbox.pad_x);
    const float pad_y = static_cast<float>(letterbox.pad_y);

    // 置信度过滤、xywh 转 xyxy，并用 letterbox 参数反变换回原图坐标。
    struct Candidate {
      float score;
      cv::Rect2f box;
      int index;
    };
    std::vector<Candidate> candidates;
    std::vector<cv::Rect2f> boxes;
    std::vector<float> scores;
    for (int i = 0; i < num_preds; ++i) {
      const float score = predictions[4 * num_preds + i];
      if (!(score >= impl_->config.candidate_conf))
        continue;
      const float cx = predictions[0 * num_preds + i];
      const float cy = predictions[1 * num_preds + i];
      const float box_w = predictions[2 * num_preds + i] / letterbox.ratio;
      const float box_h = predictions[3 * num_preds + i] / letterbox.ratio;
      cv::Rect2f box((cx - predictions[2 * num_preds + i] / 2 - pad_x) / letterbox.ratio,
                     (cy - predictions[3 * num_preds + i] / 2 - pad_y) / letterbox.ratio, box_w,
                     box_h);
      box.x = std::clamp(box.x, 0.F, static_cast<float>(image.width));
      box.y = std::clamp(box.y, 0.F, static_cast<float>(image.height));
      box.width = std::clamp(box.width, 0.F, static_cast<float>(image.width) - box.x);
      box.height = std::clamp(box.height, 0.F, static_cast<float>(image.height) - box.y);
      // 面积占比过小的候选多为反光或噪点，在进入时序窗口前就丢弃。
      if (box.width < 1.F || box.height < 1.F ||
          box.width * box.height / image_area < impl_->config.min_area_ratio)
        continue;
      candidates.push_back({score, box, i});
      boxes.push_back(box);
      scores.push_back(score);
    }

    output->clear();
    if (candidates.empty())
      return CVSDK_OK;

    const float scale_x = static_cast<float>(proto_w) / impl_->input_w;
    const float scale_y = static_cast<float>(proto_h) / impl_->input_h;
    const size_t proto_plane = static_cast<size_t>(proto_h) * proto_w;

    for (const int kept : Nms(boxes, scores, impl_->config.iou_threshold)) {
      const Candidate& candidate = candidates[static_cast<size_t>(kept)];
      std::vector<float> coefficients(static_cast<size_t>(num_masks));
      for (int m = 0; m < num_masks; ++m)
        coefficients[static_cast<size_t>(m)] =
            predictions[(4 + 1 + m) * num_preds + candidate.index];

      // 掩码系数与原型做加权和，再过 sigmoid 得到 proto 分辨率的响应图。
      cv::Mat proto_mask(proto_h, proto_w, CV_32F);
      for (int y = 0; y < proto_h; ++y) {
        float* row = proto_mask.ptr<float>(y);
        for (int x = 0; x < proto_w; ++x) {
          const size_t offset = static_cast<size_t>(y) * proto_w + x;
          float sum = 0.F;
          for (int m = 0; m < num_masks; ++m)
            sum += coefficients[static_cast<size_t>(m)] *
                   prototypes[static_cast<size_t>(m) * proto_plane + offset];
          row[x] = Sigmoid(sum);
        }
      }

      const int left = ClampInt(static_cast<int>(std::floor(candidate.box.x)), 0,
                                static_cast<int>(image.width) - 1);
      const int top = ClampInt(static_cast<int>(std::floor(candidate.box.y)), 0,
                               static_cast<int>(image.height) - 1);
      const int right = ClampInt(static_cast<int>(std::ceil(candidate.box.x + candidate.box.width)),
                                 left + 1, static_cast<int>(image.width));
      const int bottom =
          ClampInt(static_cast<int>(std::ceil(candidate.box.y + candidate.box.height)), top + 1,
                   static_cast<int>(image.height));

      // 只取框内区域放大，避免框外残余响应污染面积与质心。
      const int crop_left = ClampInt(
          static_cast<int>(std::floor((left * letterbox.ratio + pad_x) * scale_x)), 0, proto_w - 1);
      const int crop_top = ClampInt(
          static_cast<int>(std::floor((top * letterbox.ratio + pad_y) * scale_y)), 0, proto_h - 1);
      const int crop_right =
          ClampInt(static_cast<int>(std::ceil((right * letterbox.ratio + pad_x) * scale_x)),
                   crop_left + 1, proto_w);
      const int crop_bottom =
          ClampInt(static_cast<int>(std::ceil((bottom * letterbox.ratio + pad_y) * scale_y)),
                   crop_top + 1, proto_h);

      cv::Mat crop;
      cv::resize(proto_mask(cv::Rect(crop_left, crop_top, crop_right - crop_left,
                                     crop_bottom - crop_top)),
                 crop, {right - left, bottom - top}, 0, 0, cv::INTER_LINEAR);
      cv::Mat binary;
      cv::threshold(crop, binary, impl_->config.mask_threshold, 255, cv::THRESH_BINARY);
      binary.convertTo(binary, CV_8UC1);

      LeakItem item;
      item.x = candidate.box.x;
      item.y = candidate.box.y;
      item.width = candidate.box.width;
      item.height = candidate.box.height;
      item.score = candidate.score;
      item.mask_width = static_cast<int>(image.width);
      item.mask_height = static_cast<int>(image.height);
      item.mask.assign(static_cast<size_t>(image.width) * image.height, 0);
      // 框外像素保持为 0，掩码以外的区域不参与面积统计。
      for (int y = 0; y < binary.rows; ++y)
        std::memcpy(item.mask.data() + static_cast<size_t>(top + y) * image.width + left,
                    binary.ptr<uint8_t>(y), static_cast<size_t>(binary.cols));
      const cv::Moments moments = cv::moments(binary, true);
      item.area = static_cast<uint32_t>(moments.m00);
      if (moments.m00 > 0.0) {
        item.centroid_x = static_cast<float>(moments.m10 / moments.m00) + left;
        item.centroid_y = static_cast<float>(moments.m01 / moments.m00) + top;
      }
      output->push_back(std::move(item));
    }
    return CVSDK_OK;
  } catch (const Ort::Exception& e) {
    SetLastError(std::string("leak inference failed: ") + e.what());
    return CVSDK_INTERNAL_ERROR;
  } catch (const cv::Exception& e) {
    SetLastError(std::string("leak image processing failed: ") + e.what());
    return CVSDK_INTERNAL_ERROR;
  }
}
} // namespace cvsdk
