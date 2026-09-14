#include "cv_sdk/cv_sdk.h"
#include <opencv2/imgcodecs.hpp>
#include <cstdio>
#include <cmath>
#include <vector>

int main() {
  cv::Mat image = cv::imread("tests/data/gauge/2_63.jpg");
  if (image.empty()) {
    std::fprintf(stderr, "gauge regression image is unavailable\n");
    return 2;
  }
  CVSDK_GaugeReaderOptions options{sizeof(options), "onnxruntime", .25F, .25F, 0.F, 100.F,
                                   "", 1, 0, {0}};
  CVSDK_GaugeReader* reader = nullptr;
  if (CVSDK_GaugeReaderCreate("models/gauge_reader_640", &options, &reader) != CVSDK_OK) {
    std::fprintf(stderr, "%s\n", CVSDK_GetLastError());
    return 1;
  }
  CVSDK_Image input{sizeof(input), image.data, (uint32_t)image.cols, (uint32_t)image.rows,
                    (uint32_t)image.step, CVSDK_PIXEL_FORMAT_BGR8};
  uint32_t count = 0;
  CVSDK_Status status = CVSDK_GaugeReaderInfer(reader, &input, nullptr, 0, &count);
  if (status != CVSDK_BUFFER_TOO_SMALL || count == 0) {
    std::fprintf(stderr, "gauge query failed: %d %s\n", status, CVSDK_GetLastError());
    CVSDK_GaugeReaderDestroy(reader);
    return 1;
  }
  std::vector<CVSDK_GaugeReading> readings(count);
  status = CVSDK_GaugeReaderInfer(reader, &input, readings.data(), (uint32_t)readings.size(), &count);
  CVSDK_GaugeReaderDestroy(reader);
  // ONNX package baseline for this sample with range [0, 100] and calibration enabled.
  if (status != CVSDK_OK || readings[0].status != 0 ||
      std::abs(readings[0].value - 15.229F) > 0.05F) {
    std::fprintf(stderr, "gauge inference failed: %d value=%f ratio=%f %s\n", status,
                 readings[0].value, readings[0].ratio, CVSDK_GetLastError());
    return 1;
  }
  return 0;
}
