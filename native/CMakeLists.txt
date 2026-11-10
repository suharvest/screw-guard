cmake_minimum_required(VERSION 3.16)
project(screw_guard_native CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -O2)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(nlohmann_json QUIET)

set(SCREW_GUARD_SOURCES
    app_config.cpp
    result_payload.cpp
    screw_inspector.cpp
)

add_library(screw_guard_lib STATIC ${SCREW_GUARD_SOURCES})
target_include_directories(screw_guard_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(screw_guard_lib PUBLIC
    ${OpenCV_LIBS}
)
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(screw_guard_lib PUBLIC nlohmann_json::nlohmann_json)
endif()

add_executable(test_screw_inspector test_screw_inspector.cpp)
target_link_libraries(test_screw_inspector PRIVATE screw_guard_lib)
target_compile_definitions(test_screw_inspector PRIVATE SCREW_GUARD_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/result_schema.json")

add_executable(screw_guard_cli screw_guard_cli.cpp)
target_link_libraries(screw_guard_cli PRIVATE screw_guard_lib ${OpenCV_LIBS})

add_executable(test_config_loader test_config_loader.cpp)
target_link_libraries(test_config_loader PRIVATE screw_guard_lib)

enable_testing()
add_test(NAME test_screw_inspector COMMAND test_screw_inspector)
add_test(NAME test_config_loader COMMAND test_config_loader)
