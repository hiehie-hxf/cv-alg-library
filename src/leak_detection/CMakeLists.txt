cmake_minimum_required(VERSION 3.20)
project(leak_detection LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs)

# ---------------- ONNX Runtime 定位 ----------------
# 顺序：系统安装版 -> 工程内 third_party/onnxruntime -> stub 模式
set(LEAK_ORT_BUNDLED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
set(LEAK_ORT_TARGET "")

# 注意：不要把 LEAK_ORT_BUNDLED_INC / _LIB 预置成空字符串，
# CMake 的 find_* 在变量已为 OFF 类值（含空串）时会直接跳过搜索。

# 1) 系统安装版
find_package(ONNXRuntime QUIET)
if(ONNXRuntime_FOUND)
    if(TARGET onnxruntime::onnxruntime)
        set(LEAK_ORT_TARGET onnxruntime::onnxruntime)
    elseif(TARGET onnxruntime)
        set(LEAK_ORT_TARGET onnxruntime)
    endif()
endif()

# 2) 工程内 third_party：直接定位头文件和库，
#    刻意不使用 SDK 自带的 cmake config —— 它把路径写成 lib64/ 和 include/onnxruntime/，
#    而实际布局是 lib/ 和 include/，两者对不上。
if(NOT LEAK_ORT_TARGET)
    find_path(LEAK_ORT_BUNDLED_INC onnxruntime_cxx_api.h
        PATHS "${LEAK_ORT_BUNDLED_ROOT}/include"
        NO_DEFAULT_PATH)
    find_library(LEAK_ORT_BUNDLED_LIB NAMES onnxruntime
        PATHS "${LEAK_ORT_BUNDLED_ROOT}/lib"
        NO_DEFAULT_PATH)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(leak_detection_lib
    src/leak_detector.cpp
    src/preprocess.cpp
    src/postprocess.cpp
    src/leak_filter.cpp
)

target_include_directories(leak_detection_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(leak_detection_lib
    PUBLIC
    ${OpenCV_LIBS}
)

if(LEAK_ORT_TARGET)
    target_link_libraries(leak_detection_lib PUBLIC ${LEAK_ORT_TARGET})
    set(LEAK_ORT_ENABLED ON)
    message(STATUS "leak_detection: ONNX Runtime FOUND via find_package (${LEAK_ORT_TARGET})")
elseif(LEAK_ORT_BUNDLED_INC AND LEAK_ORT_BUNDLED_LIB)
    target_include_directories(leak_detection_lib PUBLIC ${LEAK_ORT_BUNDLED_INC})
    target_link_libraries(leak_detection_lib PUBLIC ${LEAK_ORT_BUNDLED_LIB})
    set(LEAK_ORT_ENABLED ON)
    message(STATUS "leak_detection: ONNX Runtime FOUND in third_party -> ${LEAK_ORT_BUNDLED_LIB}")
else()
    set(LEAK_ORT_ENABLED OFF)
    message(STATUS "leak_detection: ONNX Runtime NOT found -> stub mode")
endif()

if(LEAK_ORT_ENABLED)
    target_compile_definitions(leak_detection_lib PUBLIC LEAK_WITH_ONNXRUNTIME=1)
else()
    target_compile_definitions(leak_detection_lib PUBLIC LEAK_WITH_ONNXRUNTIME=0)
endif()

target_compile_features(leak_detection_lib PUBLIC cxx_std_17)

add_executable(leak_detect src/main.cpp)
target_link_libraries(leak_detect PRIVATE leak_detection_lib)

enable_testing()
add_executable(test_leak_filter tests/test_leak_filter.cpp)
target_link_libraries(test_leak_filter PRIVATE leak_detection_lib)
add_test(NAME test_leak_filter COMMAND test_leak_filter)

# 让可执行文件运行期能找到 third_party 里的 libonnxruntime.so
if(LEAK_ORT_ENABLED AND LEAK_ORT_BUNDLED_LIB)
    get_filename_component(LEAK_ORT_LIB_DIR "${LEAK_ORT_BUNDLED_LIB}" DIRECTORY)
    set_target_properties(leak_detect test_leak_filter PROPERTIES
        BUILD_RPATH "${LEAK_ORT_LIB_DIR}"
        INSTALL_RPATH "${LEAK_ORT_LIB_DIR}")
endif()

add_executable(test_postprocess tests/test_postprocess.cpp)
target_link_libraries(test_postprocess PRIVATE leak_detection_lib)
add_test(NAME test_postprocess COMMAND test_postprocess)

if(LEAK_ORT_ENABLED AND LEAK_ORT_BUNDLED_LIB)
    set_target_properties(test_postprocess PROPERTIES
        BUILD_RPATH "${LEAK_ORT_LIB_DIR}"
        INSTALL_RPATH "${LEAK_ORT_LIB_DIR}")
endif()
