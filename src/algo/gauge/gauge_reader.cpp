#include "algo/gauge/gauge_reader.h"

#include "base/status.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

namespace cvsdk {
namespace {
struct Box { float x, y, w, h, score; };
float IoU(const Box& a, const Box& b) {
  float l = std::max(a.x, b.x), t = std::max(a.y, b.y);
  float r = std::min(a.x + a.w, b.x + b.w), d = std::min(a.y + a.h, b.y + b.h);
  float inter = std::max(0.F, r - l) * std::max(0.F, d - t);
  float uni = a.w * a.h + b.w * b.h - inter;
  return uni > 0 ? inter / uni : 0.F;
}
std::vector<int> Nms(const std::vector<Box>& boxes, float threshold) {
  std::vector<int> order(boxes.size()); std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return boxes[a].score > boxes[b].score; });
  std::vector<int> keep;
  while (!order.empty()) {
    int current = order.front(); keep.push_back(current); std::vector<int> next;
    for (size_t i = 1; i < order.size(); ++i)
      if (IoU(boxes[current], boxes[order[i]]) <= threshold) next.push_back(order[i]);
    order = std::move(next);
  }
  return keep;
}
} // namespace

struct GaugeReader::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "cv_sdk_gauge"};
  Ort::SessionOptions options;
  std::unique_ptr<Ort::Session> detector, pose;
  std::string detector_input, detector_output, pose_input, pose_output;
  float detection_threshold = .25F, keypoint_threshold = .25F, range_min = 0.F, range_max = 100.F;
  bool calibration = true, clamp = false;
  char unit[16]{};
};

GaugeReader::GaugeReader() = default;
GaugeReader::~GaugeReader() = default;
CVSDK_Status GaugeReader::Init(const char* package_dir, const CVSDK_GaugeReaderOptions* input) {
  if (!package_dir || !input || input->range_min >= input->range_max || input->detection_threshold < 0 ||
      input->detection_threshold > 1 || input->keypoint_threshold < 0 || input->keypoint_threshold > 1) {
    SetLastError("invalid gauge reader options"); return CVSDK_INVALID_ARGUMENT;
  }
  const char* backend = input->backend ? input->backend : "onnxruntime";
  if (std::strcmp(backend, "onnxruntime") && std::strcmp(backend, "onnx") &&
      std::strcmp(backend, "onnxruntime-cuda") && std::strcmp(backend, "onnx-cuda")) {
    SetLastError("unsupported gauge reader backend"); return CVSDK_UNSUPPORTED;
  }
#ifndef CVSDK_WITH_ONNXRUNTIME_CUDA
  if (!std::strcmp(backend, "onnxruntime-cuda") || !std::strcmp(backend, "onnx-cuda")) {
    SetLastError("ONNX Runtime CUDA provider is not enabled in this build"); return CVSDK_UNSUPPORTED;
  }
#endif
  try {
    impl_ = std::make_unique<Impl>(); impl_->options.SetIntraOpNumThreads(4);
    impl_->options.AddConfigEntry("session.intra_op.allow_spinning", "0");
    impl_->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef CVSDK_WITH_ONNXRUNTIME_CUDA
    if (!std::strcmp(backend, "onnxruntime-cuda") || !std::strcmp(backend, "onnx-cuda")) {
      OrtCUDAProviderOptions cuda{}; cuda.device_id = 0; impl_->options.AppendExecutionProvider_CUDA(cuda);
    }
#endif
    const auto root = std::filesystem::path(package_dir) / "artifacts/onnxruntime";
    const auto detector_path = root / "detector.onnx", pose_path = root / "pose.onnx";
    if (!std::filesystem::is_regular_file(detector_path) || !std::filesystem::is_regular_file(pose_path)) {
      SetLastError("gauge model package requires artifacts/onnxruntime/detector.onnx and pose.onnx");
      return CVSDK_NOT_FOUND;
    }
    impl_->detector = std::make_unique<Ort::Session>(impl_->env, detector_path.c_str(), impl_->options);
    impl_->pose = std::make_unique<Ort::Session>(impl_->env, pose_path.c_str(), impl_->options);
    Ort::AllocatorWithDefaultOptions allocator;
    impl_->detector_input = impl_->detector->GetInputNameAllocated(0, allocator).get();
    impl_->detector_output = impl_->detector->GetOutputNameAllocated(0, allocator).get();
    impl_->pose_input = impl_->pose->GetInputNameAllocated(0, allocator).get();
    impl_->pose_output = impl_->pose->GetOutputNameAllocated(0, allocator).get();
    impl_->detection_threshold = input->detection_threshold; impl_->keypoint_threshold = input->keypoint_threshold;
    impl_->range_min = input->range_min; impl_->range_max = input->range_max;
    impl_->calibration = input->apply_calibration != 0; impl_->clamp = input->clamp_to_range != 0;
    if (input->unit) std::snprintf(impl_->unit, sizeof(impl_->unit), "%s", input->unit);
    return CVSDK_OK;
  } catch (const Ort::Exception& e) { SetLastError(std::string("gauge model load failed: ") + e.what()); return CVSDK_INTERNAL_ERROR; }
}

CVSDK_Status GaugeReader::Infer(const CVSDK_Image& image, std::vector<CVSDK_GaugeReading>* output) {
  if (!impl_ || !output || !image.data) { SetLastError("gauge reader is not initialized"); return CVSDK_INVALID_ARGUMENT; }
  try {
    cv::Mat source((int)image.height, (int)image.width, CV_8UC3, const_cast<uint8_t*>(image.data), image.stride_bytes), bgr;
    if (image.pixel_format == CVSDK_PIXEL_FORMAT_RGB8) cv::cvtColor(source, bgr, cv::COLOR_RGB2BGR); else bgr = source;
    auto run = [&](Ort::Session& session, const std::string& input_name, const std::string& output_name,
                   const cv::Mat& frame, std::vector<float>* tensor, float* ratio, int* left, int* top) {
      *ratio = std::min(640.F / frame.cols, 640.F / frame.rows); int w = std::lround(frame.cols * *ratio), h = std::lround(frame.rows * *ratio);
      // Match yibiao_v3's Ultralytics/OpenCV letterbox rounding: round(pad - 0.1).
      *left = static_cast<int>(std::floor((640 - w) / 2.F + .4F));
      *top = static_cast<int>(std::floor((640 - h) / 2.F + .4F)); cv::Mat resized, padded, rgb;
      cv::resize(frame, resized, {w, h}); cv::copyMakeBorder(resized, padded, *top, 640-h-*top, *left, 640-w-*left, cv::BORDER_CONSTANT, {114,114,114}); cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
      tensor->assign(3 * 640 * 640, 0); for (int y=0;y<640;++y) for (int x=0;x<640;++x) { auto p=rgb.at<cv::Vec3b>(y,x); for (int c=0;c<3;++c) (*tensor)[c*640*640+y*640+x]=p[c]/255.F; }
      std::array<int64_t,4> shape{1,3,640,640}; auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
      auto value=Ort::Value::CreateTensor<float>(memory,tensor->data(),tensor->size(),shape.data(),shape.size()); const char* in[]={input_name.c_str()}, *out[]={output_name.c_str()};
      return session.Run(Ort::RunOptions{nullptr},in,&value,1,out,1);
    };
    std::vector<float> tensor; float ratio; int left, top; auto detector_result = run(*impl_->detector, impl_->detector_input, impl_->detector_output, bgr, &tensor, &ratio, &left, &top);
    auto info=detector_result[0].GetTensorTypeAndShapeInfo(); auto dims=info.GetShape(); if (dims.size()!=3) { SetLastError("unsupported gauge detector output"); return CVSDK_UNSUPPORTED; }
    const float* raw=detector_result[0].GetTensorData<float>(); size_t channels=dims[1], count=dims[2]; bool cn=channels<count; if(!cn){channels=dims[2];count=dims[1];}
    std::vector<Box> boxes; for(size_t i=0;i<count;++i){auto get=[&](size_t c){return cn?raw[c*count+i]:raw[i*channels+c];}; float score=get(4); if(score<impl_->detection_threshold)continue; float w=get(2)/ratio,h=get(3)/ratio,x=(get(0)-get(2)/2-left)/ratio,y=(get(1)-get(3)/2-top)/ratio; boxes.push_back({x,y,w,h,score});}
    output->clear(); for(int index:Nms(boxes,.45F)) { auto box=boxes[index]; box.x=std::clamp(box.x,0.F,(float)image.width); box.y=std::clamp(box.y,0.F,(float)image.height); box.w=std::clamp(box.w,0.F,(float)image.width-box.x); box.h=std::clamp(box.h,0.F,(float)image.height-box.y); CVSDK_GaugeReading reading{}; reading.x=box.x;reading.y=box.y;reading.width=box.w;reading.height=box.h;reading.detection_score=box.score;std::snprintf(reading.unit,sizeof(reading.unit),"%s",impl_->unit); if(box.w<2||box.h<2){reading.status=1;output->push_back(reading);continue;} cv::Mat roi=bgr(cv::Rect((int)box.x,(int)box.y,(int)box.w,(int)box.h)); float pr;int pl,pt;auto pose_result=run(*impl_->pose,impl_->pose_input,impl_->pose_output,roi,&tensor,&pr,&pl,&pt);auto pdims=pose_result[0].GetTensorTypeAndShapeInfo().GetShape();const float* p=pose_result[0].GetTensorData<float>();size_t pc=pdims[1],pn=pdims[2];bool pcn=pc<pn;if(!pcn){pc=pdims[2];pn=pdims[1];} if(pc<13){SetLastError("unsupported gauge pose output");return CVSDK_UNSUPPORTED;} struct Candidate{float score,x1,y1,x2,y2;}; std::array<Candidate,3> selected{};std::array<bool,3> found{};for(size_t i=0;i<pn;++i){auto get=[&](size_t c){return pcn?p[c*pn+i]:p[i*pc+c];};int cls=0;float score=get(4);for(int c=1;c<3;++c)if(get(4+c)>score){score=get(4+c);cls=c;} if(score<impl_->keypoint_threshold||get(9)<impl_->keypoint_threshold||get(12)<impl_->keypoint_threshold)continue;if(!found[cls]||score>selected[cls].score){selected[cls]={score,get(7),get(8),get(10),get(11)};found[cls]=true;}}if(!found[0]||!found[1]||!found[2]){reading.status=1;output->push_back(reading);continue;} auto point=[&](float x,float y){return cv::Point2f((x-pl)/pr,(y-pt)/pr);};auto center=point(selected[0].x1,selected[0].y1),tip=point(selected[0].x2,selected[0].y2),start=point(selected[1].x2,selected[1].y2),end=point(selected[2].x2,selected[2].y2);auto angle=[&](cv::Point2f q){return std::atan2(center.y-q.y,q.x-center.x);};constexpr float pi=3.14159265358979323846F;float sweep=std::fmod(angle(start)-angle(end)+2*pi,2*pi);if(sweep<1e-6F){reading.status=1;output->push_back(reading);continue;}reading.ratio=std::fmod(angle(start)-angle(tip)+2*pi,2*pi)/sweep;reading.value=std::abs(impl_->range_min+(impl_->range_max-impl_->range_min)*reading.ratio+(impl_->calibration?(reading.ratio<=.5F?.012F:.008F):0.F));if(impl_->clamp)reading.value=std::clamp(reading.value,impl_->range_min,impl_->range_max);reading.pose_score=std::min({selected[0].score,selected[1].score,selected[2].score});reading.status=0;output->push_back(reading);}
    return CVSDK_OK;
  } catch (const Ort::Exception& e) { SetLastError(std::string("gauge inference failed: ")+e.what()); return CVSDK_INTERNAL_ERROR; }
}
} // namespace cvsdk
