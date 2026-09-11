# SDK/服务交付包配置。通过 `cpack --config build/CPackConfig.cmake` 生成压缩包。
set(CPACK_PACKAGE_NAME "cv-alg-library")
set(CPACK_PACKAGE_VENDOR "cv-alg-library")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Enterprise edge CV SDK and RTSP fire vision service")
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
