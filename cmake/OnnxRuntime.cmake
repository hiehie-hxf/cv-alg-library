set(CVSDK_ONNXRUNTIME_VERSION "1.20.1" CACHE STRING "Pinned ONNX Runtime version")
set(CVSDK_ONNXRUNTIME_ROOT "" CACHE PATH "Override ONNX Runtime C/C++ SDK root")

function(cvsdk_detect_onnxruntime_platform out_var)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" processor)
  if(APPLE AND processor MATCHES "^(arm64|aarch64)$")
    set(platform "macos-arm64")
  elseif(APPLE AND processor MATCHES "^(x86_64|amd64)$")
    set(platform "macos-x86_64")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND processor MATCHES "^(arm64|aarch64)$")
    set(platform "linux-aarch64")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND processor MATCHES "^(x86_64|amd64)$")
    set(platform "linux-x86_64")
  elseif(WIN32 AND processor MATCHES "^(x86_64|amd64|amd64)$")
    set(platform "windows-x86_64")
  else()
    message(FATAL_ERROR
      "Unsupported ONNX Runtime platform: system=${CMAKE_SYSTEM_NAME}, processor=${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  set(${out_var} "${platform}" PARENT_SCOPE)
endfunction()

function(cvsdk_configure_onnxruntime target)
  cvsdk_detect_onnxruntime_platform(platform)
  set(legacy_root "${PROJECT_SOURCE_DIR}/third_party/onnxruntime")
  if(CVSDK_ONNXRUNTIME_ROOT AND NOT CVSDK_ONNXRUNTIME_ROOT STREQUAL legacy_root)
    set(root "${CVSDK_ONNXRUNTIME_ROOT}")
  else()
    set(root "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/prebuilt/${platform}/${CVSDK_ONNXRUNTIME_VERSION}")
  endif()

  if(NOT EXISTS "${root}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
      "ONNX Runtime SDK not found for ${platform} ${CVSDK_ONNXRUNTIME_VERSION}.\n"
      "Expected: ${root}\n"
      "Run third_party/onnxruntime/fetch_prebuilt.sh where supported, or pass "
      "-DCVSDK_ONNXRUNTIME_ROOT=/absolute/path/to/onnxruntime.")
  endif()

  find_library(ort_library NAMES onnxruntime
    PATHS "${root}/lib" "${root}/lib64" REQUIRED NO_DEFAULT_PATH)
  target_include_directories(${target} PRIVATE "${root}/include")
  target_link_libraries(${target} PRIVATE "${ort_library}")

  if(APPLE OR UNIX)
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "${root}/lib;${root}/lib64")
  endif()
  message(STATUS "ONNX Runtime: platform=${platform}, version=${CVSDK_ONNXRUNTIME_VERSION}, root=${root}")
endfunction()
# ONNX Runtime 平台探测和依赖定位模块。
# 只允许加载与当前系统/CPU 架构匹配的 SDK，避免误链接 macOS dylib 到 Linux/Jetson。
