# 测试目录

| 目录 | 作用 | 执行方式 |
|---|---|---|
| `unit/` | 单模块、API 契约与异常输入测试 | `ctest --test-dir build` |
| `regression/` | 固定输入/预期输出的行为回归 | `ctest --test-dir build` |
| `stress/` | 长时间循环、资源稳定性、Sanitizer 检测 | `./build/cv_sdk_fire_filter_stress [iterations]` |
| `sample/` | 业务方调用示例；不作为 SDK 行为断言 | 见各子目录 README |

真实 ONNX Runtime 后端接入后，`regression/` 必须增加：测试图片、预期检测框、分数容差、模型 hash 与设备 benchmark 基线。不要将没有授权的现场图片或大模型直接提交 Git；以受控测试数据包或对象存储 artifact 提供。
