cmake_minimum_required(VERSION 3.5)
set(SSCMA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../recamera/sscma-example-sg200x" CACHE PATH "SSCMA example root")
if(NOT EXISTS "${SSCMA_ROOT}/cmake/project.cmake")
  message(FATAL_ERROR "SSCMA_ROOT must point at sscma-example-sg200x")
endif()
get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(ROOT_DIR "${SSCMA_ROOT}")
include("${SSCMA_ROOT}/cmake/toolchain-riscv64-linux-musl-x86_64.cmake")
project(screw-guard VERSION 1.0.0 LANGUAGES C CXX)
include("${SSCMA_ROOT}/cmake/project.cmake")
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../screw-guard.json" DESTINATION /usr/share/supervisor/apps)

# Follow the existing region-motion package's private runtime layout.
set(_opencv_runtime_dir
  "${SG200X_SDK_PATH}/cvi_mpi/modules/isp/cv181x/isp-tool-daemon/isp_daemon_tool/install/lib")
set(_private_runtime_libs
  "${_opencv_runtime_dir}/libopencv_core.so.4.5"
  "${_opencv_runtime_dir}/libopencv_imgproc.so.4.5"
  "${_opencv_runtime_dir}/libopencv_imgcodecs.so.4.5"
  "${SG200X_SDK_PATH}/cvi_mpi/lib/libsns_gc2053.so")
foreach(_runtime_lib IN LISTS _private_runtime_libs)
  if(NOT EXISTS "${_runtime_lib}")
    message(FATAL_ERROR "Missing screw-guard runtime library: ${_runtime_lib}")
  endif()
  install(FILES "${_runtime_lib}" DESTINATION /usr/lib/screw-guard)
endforeach()
