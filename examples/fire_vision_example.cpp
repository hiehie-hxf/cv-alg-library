#include "cv_sdk/cv_sdk.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> running{true};
void Stop(int) {
  running.store(false);
}
bool IsNumber(const std::string& value) {
  if (value.empty())
    return false;
  for (char c : value)
    if (c < '0' || c > '9')
      return false;
  return true;
}
CVSDK_Image SdkImage(const cv::Mat& image) {
  return {sizeof(CVSDK_Image),
          image.data,
          static_cast<uint32_t>(image.cols),
          static_cast<uint32_t>(image.rows),
          static_cast<uint32_t>(image.step),
          CVSDK_PIXEL_FORMAT_BGR8};
}
const char* LevelName(CVSDK_FireAlertLevel level) {
  switch (level) {
  case CVSDK_FIRE_ALERT_INFO:
    return "info";
  case CVSDK_FIRE_ALERT_WARNING_SMOKE:
    return "warning_smoke";
  case CVSDK_FIRE_ALERT_WARNING_FIRE:
    return "warning_fire";
  case CVSDK_FIRE_ALERT_CRITICAL:
    return "critical";
  default:
    return "none";
  }
}
struct LatestFrame {
  std::mutex mutex;
  std::condition_variable ready;
  cv::Mat image;
  uint64_t sequence = 0;
  bool closed = false;
};
struct LatestResult {
  std::mutex mutex;
  std::vector<CVSDK_Detection> detections;
  CVSDK_FireAlertState state{sizeof(CVSDK_FireAlertState)};
  uint64_t sequence = 0;
  double inference_ms = 0;
};
struct ResultSnapshot {
  std::vector<CVSDK_Detection> detections;
  CVSDK_FireAlertState state{sizeof(CVSDK_FireAlertState)};
  uint64_t sequence = 0;
  double inference_ms = 0;
};
void Draw(cv::Mat* frame, const ResultSnapshot& result) {
  for (const auto& d : result.detections) {
    cv::Scalar color = d.class_id == 1 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 200, 255);
    cv::Rect box((int)d.x, (int)d.y, (int)d.width, (int)d.height);
    box &= cv::Rect(0, 0, frame->cols, frame->rows);
    if (box.area() <= 0)
      continue;
    cv::rectangle(*frame, box, color, 2);
    std::ostringstream label;
    label << (d.class_id == 1 ? "fire" : "smoke") << ' ' << std::fixed << std::setprecision(2)
          << d.score;
    cv::putText(*frame, label.str(), box.tl() + cv::Point(0, -5), cv::FONT_HERSHEY_SIMPLEX, .55,
                color, 2);
  }
  std::ostringstream text;
  text << LevelName(result.state.level) << " fire=" << std::fixed << std::setprecision(2)
       << result.state.max_fire_confidence << " smoke=" << result.state.max_smoke_confidence
       << " infer=" << std::setprecision(0) << result.inference_ms << "ms";
  cv::putText(*frame, text.str(), {12, 30}, cv::FONT_HERSHEY_SIMPLEX, .7,
              result.state.level >= CVSDK_FIRE_ALERT_WARNING_FIRE ? cv::Scalar(0, 0, 255)
                                                                  : cv::Scalar(255, 255, 255),
              2);
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr
        << "usage: " << argv[0]
        << " <model_package> <fire_rules.json> <camera-index|rtsp-url|video|image> [--headless]\n";
    return 2;
  }
  const std::string source = argv[3];
  const bool headless = argc > 4 && std::string(argv[4]) == "--headless";
  std::signal(SIGINT, Stop);
  std::signal(SIGTERM, Stop);
  CVSDK_DetectorOptions options{sizeof(options), "onnxruntime", .10F, {0}};
  CVSDK_Detector* detector = nullptr;
  if (CVSDK_DetectorCreate(argv[1], &options, &detector) != CVSDK_OK) {
    std::cerr << "detector create failed: " << CVSDK_GetLastError() << '\n';
    return 1;
  }
  CVSDK_FireSmokeProcessor* processor = nullptr;
  if (CVSDK_FireSmokeProcessorCreate(argv[2], &processor) != CVSDK_OK) {
    std::cerr << "processor create failed: " << CVSDK_GetLastError() << '\n';
    CVSDK_DetectorDestroy(detector);
    return 1;
  }
  const cv::Mat still = cv::imread(source);
  const bool single_image = !still.empty();
  cv::VideoCapture capture;
  if (!single_image) {
    if (IsNumber(source))
      capture.open(std::stoi(source), cv::CAP_ANY);
    else
      capture.open(source, cv::CAP_ANY);
    capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
  }
  if (!single_image && !capture.isOpened()) {
    std::cerr << "cannot open source: " << source << '\n';
    if (IsNumber(source))
      std::cerr << "On macOS, allow Camera access for Codex/Terminal in System Settings > Privacy "
                   "& Security > Camera.\n";
    CVSDK_FireSmokeProcessorDestroy(processor);
    CVSDK_DetectorDestroy(detector);
    return 3;
  }
  LatestFrame latest_frame;
  LatestResult latest_result;
  std::thread inference([&] {
    uint64_t consumed = 0;
    while (running.load()) {
      cv::Mat frame;
      uint64_t sequence = 0;
      {
        std::unique_lock<std::mutex> lock(latest_frame.mutex);
        latest_frame.ready.wait(lock, [&] {
          return !running.load() || latest_frame.closed || latest_frame.sequence != consumed;
        });
        if (!running.load() || (latest_frame.closed && latest_frame.sequence == consumed))
          break;
        frame = latest_frame.image.clone();
        sequence = latest_frame.sequence;
        consumed = sequence;
      }
      auto started = std::chrono::steady_clock::now();
      CVSDK_Image input = SdkImage(frame);
      CVSDK_Detection raw[256];
      CVSDK_DetectionList raw_list{sizeof(raw_list), raw, 256, 0};
      if (CVSDK_DetectorInfer(detector, &input, &raw_list) != CVSDK_OK) {
        std::cerr << "inference failed: " << CVSDK_GetLastError() << '\n';
        continue;
      }
      CVSDK_Detection filtered[256];
      CVSDK_DetectionList list{sizeof(list), filtered, 256, 0};
      CVSDK_FireAlertState state{sizeof(state)};
      if (CVSDK_FireSmokeProcessorProcess(processor, &input, raw, raw_list.count, &list, &state) !=
          CVSDK_OK) {
        std::cerr << "postprocess failed: " << CVSDK_GetLastError() << '\n';
        continue;
      }
      double elapsed =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
              .count();
      {
        std::lock_guard<std::mutex> lock(latest_result.mutex);
        latest_result.detections.assign(filtered, filtered + list.count);
        latest_result.state = state;
        latest_result.sequence = sequence;
        latest_result.inference_ms = elapsed;
      }
      std::cout << "inference frame=" << sequence << " raw=" << raw_list.count
                << " filtered=" << list.count << " level=" << LevelName(state.level)
                << " infer_ms=" << elapsed << std::endl;
      if (single_image)
        break;
    }
  });
  uint64_t sequence = 0;
  while (running.load()) {
    cv::Mat frame;
    if (single_image)
      frame = still.clone();
    else if (!capture.read(frame) || frame.empty()) {
      if (IsNumber(source) || source.rfind("rtsp://", 0) == 0 || source.rfind("rtsps://", 0) == 0) {
        capture.release();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (IsNumber(source))
          capture.open(std::stoi(source), cv::CAP_ANY);
        else
          capture.open(source, cv::CAP_ANY);
        continue;
      }
      break;
    }
    ++sequence;
    {
      std::lock_guard<std::mutex> lock(latest_frame.mutex);
      latest_frame.image = frame.clone();
      latest_frame.sequence = sequence;
    }
    latest_frame.ready.notify_one();
    ResultSnapshot snapshot;
    {
      std::lock_guard<std::mutex> lock(latest_result.mutex);
      snapshot.detections = latest_result.detections;
      snapshot.state = latest_result.state;
      snapshot.sequence = latest_result.sequence;
      snapshot.inference_ms = latest_result.inference_ms;
    }
    if (!headless) {
      Draw(&frame, snapshot);
      cv::imshow("cv fire vision", frame);
      int key = cv::waitKey(1);
      if (key == 27 || key == 'q' || key == 'Q')
        running.store(false);
    }
    if (single_image) {
      for (;;) {
        {
          std::lock_guard<std::mutex> lock(latest_result.mutex);
          if (latest_result.sequence == sequence)
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      break;
    }
  }
  {
    std::lock_guard<std::mutex> lock(latest_frame.mutex);
    latest_frame.closed = true;
  }
  latest_frame.ready.notify_all();
  if (inference.joinable())
    inference.join();
  capture.release();
  if (!headless)
    cv::destroyAllWindows();
  CVSDK_FireSmokeProcessorDestroy(processor);
  CVSDK_DetectorDestroy(detector);
  std::cout << "stopped, captured_frames=" << sequence << '\n';
  return 0;
}
