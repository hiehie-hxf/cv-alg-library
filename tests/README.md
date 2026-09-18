# 测试目录

| 目录 | 作用 | 执行方式 |
|---|---|---|
| `unit/` | 单模块、API 契约与异常输入测试 | `ctest --test-dir build` |
| `regression/` | 固定输入/预期输出的行为回归 | `ctest --test-dir build` |
| `stress/` | 长时间循环、资源稳定性、Sanitizer 检测 | `./build/cv_sdk_fire_filter_stress [iterations]` |
| `sample/` | 业务方调用示例；不作为 SDK 行为断言 | 见各子目录 README |

真实 ONNX Runtime 后端接入后，`regression/` 必须增加：测试图片、预期检测框、分数容差、模型 hash 与设备 benchmark 基线。不要将没有授权的现场图片或大模型直接提交 Git；以受控测试数据包或对象存储 artifact 提供。

当前回归测试还包括 `regression/gauge_reader_test.cpp`，使用 `tests/data/gauge/2_63.jpg` 验证仪表读数器的模型加载、容量查询和读数输出。该测试需要 `models/gauge_reader_640/artifacts/onnxruntime/{detector,pose}.onnx` 以及 ONNX Runtime 运行时；缺少模型制品时应先准备模型包再运行。

`regression/digital_gauge_reader_test.cpp` 使用 `tests/data/digital_gauge/gauge_ocr_reference.jpg`
验证数字仪表的 3 个面板、6 行 PV/SV 读数以及 C ABI 容量查询。该算法只依赖 OpenCV，
配置包位于 `models/digital_gauge_v1/`。
