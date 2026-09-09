#include "cv_sdk/cv_sdk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  CVSDK_LogOptions log_options = {sizeof(log_options), CVSDK_LOG_INFO, NULL, 0, 0, 16, NULL, NULL};
  assert(CVSDK_ConfigureLogging(&log_options) == CVSDK_OK);
  CVSDK_Log(CVSDK_LOG_INFO, "test", "log callback smoke test");
  CVSDK_LogStats log_stats = {sizeof(log_stats), 0, 0, 0};
  assert(CVSDK_GetLogStats(&log_stats) == CVSDK_OK && log_stats.accepted_count >= 1);
  CVSDK_Detector* detector = NULL;
  CVSDK_DetectorOptions options = {sizeof(options), "mock", .25f, {0}};
  assert(CVSDK_DetectorCreate("models/demo_detector", &options, &detector) == CVSDK_OK);
  unsigned char pixels[32 * 32 * 3] = {0};
  CVSDK_Image image = {sizeof(image), pixels, 32, 32, 32 * 3, CVSDK_PIXEL_FORMAT_BGR8};
  CVSDK_DetectionList query = {sizeof(query), NULL, 0, 0};
  assert(CVSDK_DetectorInfer(detector, &image, &query) == CVSDK_BUFFER_TOO_SMALL &&
         query.count == 1);
  CVSDK_Detection item[1];
  CVSDK_DetectionList output = {sizeof(output), item, 1, 0};
  assert(CVSDK_DetectorInfer(detector, &image, &output) == CVSDK_OK && output.count == 1 &&
         item[0].score == .90f);
  CVSDK_DetectorDestroy(detector);
  puts("cv_sdk_api_test passed");
  return 0;
}
