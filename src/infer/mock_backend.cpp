#include "infer/mock_backend.h"
#include "base/status.h"
#include <filesystem>

namespace cvsdk {
CVSDK_Status MockBackend::Load(const std::string& package_dir) {
  if (!std::filesystem::is_regular_file(std::filesystem::path(package_dir) / "manifest.json")) {
    SetLastError("model package manifest.json was not found");
    return CVSDK_NOT_FOUND;
  }
  return CVSDK_OK;
}
CVSDK_Status MockBackend::Run(const CVSDK_Image& image, std::vector<Detection>* output) {
  output->clear();
  if (image.width >= 16 && image.height >= 16) {
    output->push_back(
        {image.width * .2F, image.height * .2F, image.width * .6F, image.height * .6F, .90F, 0});
  }
  return CVSDK_OK;
}
} // namespace cvsdk
