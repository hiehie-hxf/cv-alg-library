#include <iostream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "leak_detector.h"

namespace {

void DrawResult(const cv::Mat& image,
                const std::vector<leak::LeakItem>& items,
                const leak::LeakAlertState& state,
                cv::Mat* canvas) {
  *canvas = image.clone();

  for (const leak::LeakItem& it : items) {
    const cv::Rect box(static_cast<int>(it.box.x), static_cast<int>(it.box.y),
                       static_cast<int>(it.box.width), static_cast<int>(it.box.height));
    cv::rectangle(*canvas, box, cv::Scalar(0, 0, 255), 2);

    if (!it.mask.empty()) {
      cv::Mat overlay = canvas->clone();
      overlay.setTo(cv::Scalar(0, 255, 255), it.mask);
      cv::addWeighted(overlay, 0.45, *canvas, 0.55, 0.0, *canvas);
    }

    const std::string label = "liquid_stain " + std::to_string(it.confidence);
    cv::putText(*canvas, label, cv::Point(box.x, std::max(20, box.y - 8)),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
  }

  const std::string banner = state.alert ? "ALERT" : "normal";
  cv::putText(*canvas, banner, cv::Point(20, 50), cv::FONT_HERSHEY_SIMPLEX, 1.4,
              state.alert ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 180, 0), 3);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: leak_detect <model.onnx> <config.json> <image_path> [more_images...]\n"
              << "  多个图片会按顺序当作连续帧，共用同一个时序状态。\n";
    return 1;
  }

  const std::string model_path = argv[1];
  const std::string config_path = argv[2];
  const int frame_count = argc - 3;

  leak::LeakDetector detector;

  if (!detector.LoadConfig(config_path)) {
    std::cerr << "failed to load config: " << config_path << "\n";
    return 1;
  }
  if (!detector.LoadModel(model_path)) {
    std::cerr << "failed to load model: " << model_path << "\n";
    return 1;
  }

  for (int i = 3; i < argc; ++i) {
    const std::string image_path = argv[i];
    const int frame_index = i - 3;

    const cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
    if (image.empty()) {
      std::cerr << "failed to read image: " << image_path << "\n";
      return 1;
    }

    std::vector<leak::LeakItem> items;
    leak::LeakAlertState state;
    if (!detector.Infer(image, &items, &state)) {
      std::cerr << "inference failed on " << image_path << "\n";
      return 1;
    }

    std::cout << "--- frame " << frame_index << ": " << image_path << " ---\n";
    std::cout << "Detected " << items.size() << " leak(s)\n";
    for (std::size_t k = 0; k < items.size(); ++k) {
      const leak::LeakItem& it = items[k];
      std::cout << "  [" << k << "] conf=" << it.confidence
                << " box=[" << it.box.x << "," << it.box.y << ","
                << it.box.width << "," << it.box.height << "]"
                << " area=" << it.area << "\n";
    }
    std::cout << "Alert: " << (state.alert ? "YES" : "NO")
              << ", hits=" << state.hit_count
              << ", growing=" << state.area_growing
              << ", down=" << state.centroid_down << "\n";

    cv::Mat canvas;
    DrawResult(image, items, state, &canvas);

    const std::string out_name =
        (frame_count == 1) ? std::string("output.jpg")
                           : "output_" + std::to_string(frame_index) + ".jpg";
    if (cv::imwrite(out_name, canvas)) {
      std::cout << "saved " << out_name << "\n";
    } else {
      std::cerr << "failed to write " << out_name << "\n";
    }
  }

  return 0;
}
