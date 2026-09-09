# ONNX Runtime dependency

Pinned version: **1.20.1**. Platform packages are stored explicitly under:

```text
prebuilt/<platform>/<version>/
```

Current bundled development package:

```text
prebuilt/macos-arm64/1.20.1/
```

It is the official `onnxruntime-osx-arm64-1.20.1.tgz` package and can only run
on Apple Silicon macOS. It must never be copied to Linux or Jetson devices.

Install the audited package on Apple Silicon macOS:

```sh
bash third_party/onnxruntime/fetch_prebuilt.sh
```

For Jetson/Linux AArch64, prepare a C/C++ ONNX Runtime distribution compatible
with the target JetPack, CUDA, cuDNN and glibc, then either place it at:

```text
prebuilt/linux-aarch64/1.20.1/
```

or configure it explicitly:

```sh
cmake -S . -B build \
  -DCVSDK_ONNXRUNTIME_ROOT=/opt/onnxruntime-linux-aarch64
```

Licenses and notices are preserved in `licenses/`. Package URLs, hashes and
provider expectations are recorded in `version.lock.json`.
