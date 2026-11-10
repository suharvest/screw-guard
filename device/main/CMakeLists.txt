# Use the SDK's single json version (3.12) for native and SDK components;
# the host-only bundled native header is 3.11.3.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann")
configure_file("${SSCMA_ROOT}/components/json/json.hpp"
  "${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann/json.hpp" COPYONLY)
file(GLOB_RECURSE srcs "${CMAKE_CURRENT_LIST_DIR}/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/*.c")
component_register(COMPONENT_NAME main SRCS ${srcs}
    "${CMAKE_CURRENT_LIST_DIR}/../../native/app_config.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../native/result_payload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../native/screw_inspector.cpp"
  INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/include" "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../native"
  PRIVATE_REQUIREDS app_framework sophgo rtsp_server ha_mqtt debug_stream json sscma-micro
  REQUIREDS app_framework sophgo rtsp_server ha_mqtt debug_stream json sscma-micro opencv_core opencv_imgproc opencv_imgcodecs)
