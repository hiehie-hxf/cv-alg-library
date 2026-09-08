cmake_minimum_required(VERSION 3.20)
project(cv_alg_library VERSION 0.1.0 LANGUAGES C CXX)

option(CVSDK_BUILD_TESTS "Build SDK tests" ON)
option(CVSDK_BUILD_EXAMPLES "Build SDK examples" ON)
option(CVSDK_WITH_ONNXRUNTIME "Enable ONNX Runtime backend when available" OFF)

add_library(cv_sdk SHARED
  src/api/cv_sdk.cpp
  src/base/status.cpp
  src/base/logger.cpp
  src/infer/mock_backend.cpp
  src/algo/detector.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(cv_sdk PRIVATE Threads::Threads)
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
  message(FATAL_ERROR "ONNX Runtime adapter is intentionally not bundled in the MVP. Add it as a backend package after selecting its target version.")
endif()

if(CVSDK_BUILD_EXAMPLES)
  add_executable(cv_sdk_detect_example examples/detect_example.c)
  target_link_libraries(cv_sdk_detect_example PRIVATE cv_sdk::cv_sdk)
endif()

if(CVSDK_BUILD_TESTS)
  enable_testing()
  add_executable(cv_sdk_api_test tests/unit/api_test.c)
  target_link_libraries(cv_sdk_api_test PRIVATE cv_sdk::cv_sdk)
  add_test(NAME cv_sdk_api_test COMMAND cv_sdk_api_test)
  set_tests_properties(cv_sdk_api_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

install(TARGETS cv_sdk EXPORT cv_sdkTargets)
install(FILES include/cv_sdk/cv_sdk.h DESTINATION include/cv_sdk)
