# 第三方依赖隔离

此目录只放可复现获取的第三方源码、补丁、构建脚本或下载说明；**不得**让其公开头文件进入 `include/cv_sdk/`。

建议布局：

```text
third_party/
├── onnxruntime/     # 平台化 prebuilt、下载脚本、版本/哈希锁与许可证
├── opencv/          # 可选：图像算子和烟雾静态门控
├── tensorrt/        # 仅发现/链接脚本，JetPack 提供运行时
├── rknn/            # 厂商 SDK 适配构建脚本
└── licenses/        # NOTICE、许可证和审计记录
```

生产构建通过 CMake `find_package` 或受控 toolchain 指定依赖位置，严禁在业务/公开 API 中暴露 ORT、OpenCV、TensorRT、RKNN 类型。
