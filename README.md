# cv-alg-library

面向工业边缘端的 C++17 CV 推理 SDK MVP。运行时不依赖 Python，对外仅暴露稳定的 C ABI。

当前实现刻意收敛：Linux/macOS、同步检测 API、模型包目录、可验证的 mock 后端。`InferBackend` 是 TensorRT、ONNX Runtime、RKNN 等后端的扩展点；真实硬件模型必须作为各自经过验证的 artifact 交付，不能假定同一模型可无差异切换。

## 文档

- [SDK 使用文档](docs/sdk_usage.md)：集成方式、API 参考、配置参考、告警语义与排错
- [架构说明](docs/architecture.md)：分层结构、服务层边界与后端扩展点
- [测试说明](tests/README.md)：测试分层与执行方式
- [第三方依赖](third_party/README.md)：依赖隔离规范
- [服务接口](apps/cv_fire_vision_service/README.md)：RTSP 服务的 HTTP/JSON 契约

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

## 火焰/烟雾 JSON 配置与规则

`models/fire_smoke_640/fire_rules.json` 是火焰算法的 C++ 配置样例。它只包含可现场调节的候选阈值、面积阈值和多帧确认规则；模型输入尺寸、类别顺序、归一化与输出格式必须留在模型包 manifest，不能由站点配置覆盖。

用 `CVSDK_FireFilterCreate` 加载 JSON，使用 `CVSDK_FireFilterProcess` 处理每帧的原始检测框。类别约定固定为 `0=smoke`、`1=fire`，每路相机必须创建独立的 filter，避免多路视频混用时间窗口状态。加载器会拒绝不合法范围、缺失字段，以及 `min_hits > window` 等组合错误。

`CVSDK_FireSmokeProcessor` 是完整火情后处理入口：原始框先经过火焰颜色门控，再进入面积/时序告警规则；它会返回过滤后的框和告警状态。算法领域层目录说明位于 `src/algo/`，第三方依赖隔离规范见 [third_party/README.md](third_party/README.md)。

识别阈值在 `models/fire_smoke_640/fire_rules.json` 中调整：

```json
"candidate_thresholds": {"fire": 0.25, "smoke": 0.10}
```

`fire` 和 `smoke` 使用独立阈值。提高阈值会减少低置信度框和误报，但可能降低召回；建议先调 `fire`，烟雾通常保留较低阈值，再通过多帧确认和静态/光晕过滤抑制误报。修改后重新创建 `CVSDK_FireSmokeProcessor`，新配置才会生效。

第一阶段后处理还会在时序窗口内按 IoU 关联同一个目标，避免不同位置的偶发框被累计为同一事件。火焰确认除了窗口命中数，还要求达到 `fire_confirm_conf` 的命中数；严重火焰告警要求 `critical_fire_conf` 连续命中。`track_iou_threshold` 控制目标关联阈值，`track_max_missed` 控制允许短暂丢帧数。摄像头整体运动超过 `global_motion_max_ratio` 时，当前帧不累计烟雾证据。

真实模型已复制到 `models/fire_smoke_640/artifacts/onnxruntime/model.onnx`，哈希和类别契约记录在该目录的 `manifest.json` 与 `model_card.md`。模型文件默认被 `.gitignore` 排除，建议通过模型制品库交付。

业务运行示例：

```sh
./build/cv_sdk_fire_vision_example models/fire_smoke_640 models/fire_smoke_640/fire_rules.json 0
./build/cv_sdk_fire_vision_example models/fire_smoke_640 models/fire_smoke_640/fire_rules.json rtsp://user:password@host/live --headless
```

macOS 首次运行本地摄像头时，需要在“系统设置 → 隐私与安全性 → 相机”中允许 Codex 或启动程序所用的 Terminal 访问相机。当前 macOS ARM64 CPU 实测 `fire01.jpg` 的 640 ONNX 单帧完整链路约为 1.2 秒；实时边缘部署应使用 Jetson CUDA/TensorRT。

业务示例使用“最新帧”异步流水线：取流和显示不等待推理，推理线程只消费最新帧并丢弃积压旧帧；前台持续绘制最近一次检测结果。因此预览保持流畅，检测框的更新频率取决于模型推理速度。

macOS 开发包默认使用 CPU Execution Provider。该 YOLO 模型包含 CoreML 不支持的大维度检测头，CoreML 只能部分执行且在受限运行环境可能无法创建编译缓存，因此不默认启用。生产实时部署建议使用 Jetson CUDA/TensorRT 后端。

测试分层与执行方式见 [tests/README.md](tests/README.md)。

## Artifact 包

MVP 将 `.cvmodel` 解包后的目录作为输入，至少要求 `manifest.json`。生产版应增加哈希、签名和硬件兼容字段（CUDA/TensorRT 版本、GPU compute capability、RK 芯片型号等）。

## RTSP 服务与 SDK 交付

仓库中的 `apps/cv_fire_vision_service` 是平台侧可调用的独立服务：服务负责 RTSP 拉流、断线重连、每路流独立推理和 HTTP/JSON 接口，SDK 只负责同步图像推理与火情后处理。详细接口和请求示例见 [apps/cv_fire_vision_service/README.md](apps/cv_fire_vision_service/README.md)。

构建并生成安装目录：

```sh
cmake -S . -B build -DCVSDK_BUILD_SERVICE=ON -DCVSDK_BUILD_TESTS=ON
cmake --build build -j4
cmake --install build --prefix dist/cv-alg-library
```

生成交付压缩包：

```sh
cpack --config build/CPackConfig.cmake
```

交付包包含 `include/cv_sdk/cv_sdk.h`、`libcv_sdk` 动态库、`cv_fire_vision_service` 服务程序，以及 JSON/manifest/model card 等模型包元数据。ONNX 模型二进制默认不进入 Git，应由模型制品库或单独的模型包发布；部署时将 `artifacts/onnxruntime/model.onnx` 放入对应模型目录。
