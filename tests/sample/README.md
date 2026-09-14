# 业务调用示例

- `cpp/detect_example.c`：C/C++ 调用检测 SDK；CMake 会生成 `cv_sdk_detect_example`。
- `python/ctypes_fire_filter.py`：不用 pybind11，直接通过稳定 C ABI 调用火情规则模块。
- 仪表读数调用示例见 [`docs/gauge_reader_usage.md`](../../docs/gauge_reader_usage.md)，当前未单独提供 sample 可执行文件。

示例仅展示调用方式；生产应用必须检查每个状态码，并记录 `CVSDK_GetLastError()` 返回的错误详情。
