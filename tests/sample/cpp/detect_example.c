#include "cv_sdk/cv_sdk.h"
/* 面向业务方的最小 C ABI 调用示例；生产代码必须完整检查错误码。 */
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <model-package-dir>\n", argv[0]);
    return 2;
  }
  CVSDK_Detector* detector = NULL;
  CVSDK_DetectorOptions options = {sizeof(options), "mock", 0.25f, {0}};
  if (CVSDK_DetectorCreate(argv[1], &options, &detector) != CVSDK_OK) {
    fprintf(stderr, "%s\n", CVSDK_GetLastError());
    return 1;
  }
  unsigned char pixels[32 * 32 * 3] = {0};
  CVSDK_Image image = {sizeof(image), pixels, 32, 32, 32 * 3, CVSDK_PIXEL_FORMAT_BGR8};
  CVSDK_Detection item[4];
  CVSDK_DetectionList results = {sizeof(results), item, 4, 0};
  CVSDK_Status status = CVSDK_DetectorInfer(detector, &image, &results);
  printf("status=%d, detections=%u\n", status, results.count);
  CVSDK_DetectorDestroy(detector);
  return status != CVSDK_OK;
}
