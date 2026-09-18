#include "cv_sdk/cv_sdk.h"
#include <cstdio>
#include <opencv2/imgcodecs.hpp>
#include <string>
#include <vector>

int main() {
  cv::Mat image = cv::imread("tests/data/digital_gauge/gauge_ocr_reference.jpg");
  if (image.empty()) {
    std::fprintf(stderr, "digital gauge regression image is unavailable\n");
    return 2;
  }
  CVSDK_DigitalGaugeReaderOptions options{sizeof(options), 0.F, 1, {0}};
  CVSDK_DigitalGaugeReader* reader = nullptr;
  if (CVSDK_DigitalGaugeReaderCreate("models/digital_gauge_v1", &options, &reader) != CVSDK_OK) {
    std::fprintf(stderr, "%s\n", CVSDK_GetLastError());
    return 1;
  }
  CVSDK_Image input{sizeof(input),        image.data,           (uint32_t)image.cols,
                    (uint32_t)image.rows, (uint32_t)image.step, CVSDK_PIXEL_FORMAT_BGR8};
  uint32_t count = 0;
  CVSDK_Status status = CVSDK_DigitalGaugeReaderInfer(reader, &input, nullptr, 0, &count);
  if (status != CVSDK_BUFFER_TOO_SMALL || count != 6) {
    std::fprintf(stderr, "digital gauge query failed: status=%d count=%u %s\n", status, count,
                 CVSDK_GetLastError());
    CVSDK_DigitalGaugeReaderDestroy(reader);
    return 1;
  }
  std::vector<CVSDK_DigitalGaugeReading> rows(count);
  status = CVSDK_DigitalGaugeReaderInfer(reader, &input, rows.data(), count, &count);
  CVSDK_DigitalGaugeReaderDestroy(reader);
  const char* expected[] = {"176", "179.0", "169", "170.0", "58.8", "58.5"};
  if (status != CVSDK_OK)
    return 1;
  for (uint32_t i = 0; i < count; ++i) {
    std::printf("%u %s confidence=%.3f specificity=%.3f flags=%u\n", i, rows[i].text,
                rows[i].confidence, rows[i].segment_specificity, rows[i].flags);
    if (std::string(rows[i].text) != expected[i] || rows[i].panel_index != i / 2 ||
        rows[i].role != (i % 2 ? CVSDK_DIGITAL_GAUGE_ROW_SV : CVSDK_DIGITAL_GAUGE_ROW_PV))
      return 1;
  }
  return 0;
}
