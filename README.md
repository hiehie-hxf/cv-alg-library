# cv-alg-library

面向工业边缘端的 C++17 CV 推理 SDK MVP。运行时不依赖 Python，对外仅暴露稳定的 C ABI。

当前实现刻意收敛：Linux/macOS、同步检测 API、模型包目录、可验证的 mock 后端。`InferBackend` 是 TensorRT、ONNX Runtime、RKNN 等后端的扩展点；真实硬件模型必须作为各自经过验证的 artifact 交付，不能假定同一模型可无差异切换。

## 构建与运行

```sh
cmake -S . -B build -DCVSDK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/cv_sdk_detect_example models/demo_detector
```

## API 契约

- 所有公开结构体以 `struct_size` 开头，以便 ABI 向后扩展。
- `CVSDK_Image::data` 的所有权始终归调用方；同步 `CVSDK_DetectorInfer` 返回前库不会保存它。
- 检测结果由调用方分配。首次可令 `items = NULL` 查询 `count`；若容量不足会返回 `CVSDK_BUFFER_TOO_SMALL` 并写入所需数量。
- API 不会让 C++ 异常越过 C ABI；失败返回 `CVSDK_Status`，详情可由 `CVSDK_GetLastError()` 获取（线程本地）。

## 日志

通过 `CVSDK_ConfigureLogging` 设置最低等级、滚动文件和业务回调。日志由 SDK 后台线程写出，推理线程只做有界入队；队列满时丢弃新日志并通过 `CVSDK_GetLogStats` 暴露 `dropped_count`，不会导致 OOM。

默认最低等级为 `WARN`，且仅输出控制台。若配置回调，回调运行在 SDK 日志线程，必须快速返回，不能阻塞或销毁 SDK。日志为单行 JSON，不应传入图像、OCR 文本、车牌或其他敏感信息。

## Artifact 包

MVP 将 `.cvmodel` 解包后的目录作为输入，至少要求 `manifest.json`。生产版应增加哈希、签名和硬件兼容字段（CUDA/TensorRT 版本、GPU compute capability、RK 芯片型号等）。
