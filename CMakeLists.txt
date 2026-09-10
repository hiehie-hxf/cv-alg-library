cmake_minimum_required(VERSION 3.20)
# 企业级 CV SDK 顶层构建入口：统一管理库、示例、测试和可选后端依赖。
project(cv_alg_library VERSION 0.1.0 LANGUAGES C CXX)

option(CVSDK_BUILD_TESTS "构建 SDK 单元/回归测试" ON)
option(CVSDK_BUILD_EXAMPLES "构建业务示例程序" ON)
option(CVSDK_WITH_ONNXRUNTIME "启用 ONNX Runtime 推理后端" ON)
option(CVSDK_WITH_OPENCV "启用 OpenCV 图像算子和火情门控" ON)

add_library(cv_sdk SHARED
  src/api/cv_sdk.cpp
  src/base/status.cpp
  src/base/logger.cpp
  src/base/json.cpp
  src/infer/mock_backend.cpp
  src/infer/onnxruntime_backend.cpp
  src/algo/detector.cpp
  src/algo/fire_smoke/fire_config.cpp
  src/algo/fire_smoke/fire_filter.cpp
  src/algo/fire_smoke/fire_color_gate.cpp
  src/algo/fire_smoke/smoke_static_gate.cpp
  src/algo/fire_smoke/fire_smoke_processor.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(cv_sdk PRIVATE Threads::Threads)
if(CVSDK_WITH_OPENCV)
  find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc videoio highgui imgcodecs)
  target_link_libraries(cv_sdk PRIVATE opencv_core opencv_imgproc)
  target_compile_definitions(cv_sdk PRIVATE CVSDK_WITH_OPENCV=1)
else()
  message(FATAL_ERROR "Fire/smoke algorithm requires OpenCV. Set OpenCV_DIR or install OpenCV 4.")
endif()
add_library(cv_sdk::cv_sdk ALIAS cv_sdk)

target_compile_features(cv_sdk PRIVATE cxx_std_17)
target_compile_definitions(cv_sdk PRIVATE CVSDK_BUILDING_LIBRARY=1)
target_include_directories(cv_sdk PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_include_directories(cv_sdk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(cv_sdk PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(CVSDK_WITH_ONNXRUNTIME)
  include(cmake/OnnxRuntime.cmake)
  cvsdk_configure_onnxruntime(cv_sdk)
  target_compile_definitions(cv_sdk PRIVATE CVSDK_WITH_ONNXRUNTIME=1)
endif()

if(CVSDK_BUILD_EXAMPLES)
  add_executable(cv_sdk_detect_example tests/sample/cpp/detect_example.c)
  target_link_libraries(cv_sdk_detect_example PRIVATE cv_sdk::cv_sdk)
  add_executable(cv_sdk_fire_vision_example examples/fire_vision_example.cpp)
  target_link_libraries(cv_sdk_fire_vision_example PRIVATE cv_sdk::cv_sdk ${OpenCV_LIBS})
endif()

if(CVSDK_BUILD_TESTS)
  enable_testing()
  add_executable(cv_sdk_api_test tests/unit/api_test.c)
  target_link_libraries(cv_sdk_api_test PRIVATE cv_sdk::cv_sdk)
  add_test(NAME cv_sdk_api_test COMMAND cv_sdk_api_test)
  set_tests_properties(cv_sdk_api_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(cv_sdk_fire_filter_test tests/unit/fire_filter_test.cpp)
  target_link_libraries(cv_sdk_fire_filter_test PRIVATE cv_sdk::cv_sdk)
  add_test(NAME cv_sdk_fire_filter_test COMMAND cv_sdk_fire_filter_test)
  set_tests_properties(cv_sdk_fire_filter_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(cv_sdk_fire_regression_test tests/regression/fire_rules_regression.cpp)
  target_link_libraries(cv_sdk_fire_regression_test PRIVATE cv_sdk::cv_sdk)
  add_test(NAME cv_sdk_fire_regression_test COMMAND cv_sdk_fire_regression_test)
  set_tests_properties(cv_sdk_fire_regression_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(cv_sdk_fire_filter_stress tests/stress/fire_filter_stress.cpp)
  target_link_libraries(cv_sdk_fire_filter_stress PRIVATE cv_sdk::cv_sdk)
  add_executable(cv_sdk_fire_smoke_processor_test tests/unit/fire_smoke_processor_test.cpp)
  target_link_libraries(cv_sdk_fire_smoke_processor_test PRIVATE cv_sdk::cv_sdk)
  add_test(NAME cv_sdk_fire_smoke_processor_test COMMAND cv_sdk_fire_smoke_processor_test)
  set_tests_properties(cv_sdk_fire_smoke_processor_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

install(TARGETS cv_sdk EXPORT cv_sdkTargets)
install(FILES include/cv_sdk/cv_sdk.h DESTINATION include/cv_sdk)
