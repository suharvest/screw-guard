file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann")
configure_file("${ROOT_DIR}/components/json/json.hpp"
  "${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann/json.hpp" COPYONLY)

file(GLOB_RECURSE srcs ${CMAKE_CURRENT_LIST_DIR}/*.c ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

set(incs ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

component_register(
    COMPONENT_NAME main
    SRCS ${srcs}
    INCLUDE_DIRS ${incs}
    PRIVATE_REQUIREDS app_framework sscma-micro rtsp_server sophgo ha_mqtt debug_stream opencv_core opencv_imgproc opencv_imgcodecs json
)
