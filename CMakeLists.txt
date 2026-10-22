cmake_minimum_required(VERSION 3.16)
project(MavlinkDebugApp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# --- Gremsy PayloadSDK (git submodule, pinned) + this app's transport-observer patch ---
set(SDK_DIR ${CMAKE_SOURCE_DIR}/third_party/PayloadSdk)
set(SDK_PATCH ${CMAKE_SOURCE_DIR}/patches/payloadsdk-transport-observer.patch)
if(NOT EXISTS ${SDK_DIR}/libs/CMakeLists.txt)
    message(FATAL_ERROR "PayloadSDK missing: run  git submodule update --init")
endif()
execute_process(COMMAND git apply --reverse --check ${SDK_PATCH}
                WORKING_DIRECTORY ${SDK_DIR} RESULT_VARIABLE sdk_unpatched OUTPUT_QUIET ERROR_QUIET)
if(NOT sdk_unpatched EQUAL 0)
    execute_process(COMMAND git apply ${SDK_PATCH} WORKING_DIRECTORY ${SDK_DIR} RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Could not apply ${SDK_PATCH} to ${SDK_DIR}")
    endif()
    message(STATUS "Applied ${SDK_PATCH}")
endif()

find_package(Threads REQUIRED)
add_subdirectory(${SDK_DIR}/libs ${CMAKE_BINARY_DIR}/payloadsdk)  # target: PayloadSDK
target_link_libraries(PayloadSDK Threads::Threads)  # plain signature: the SDK's own CMake uses it

# --- Qt-free MAVLink core (framing, decoding) ---
add_library(mavcore STATIC src/mavlink_frame.cpp)
target_include_directories(mavcore PUBLIC src ${SDK_DIR}/libs/third-party/mavlink/include/mavlink/v2.0)
target_compile_options(mavcore PRIVATE -Wall -Wextra)

# --- App logic without widgets: SDK ownership, link health, frame model ---
add_library(appcore STATIC
    src/link_health.cpp src/payload_link.cpp src/frame_model.cpp src/frame_filter.cpp
    src/frame_text.cpp src/frame_writer.cpp src/flow_stats.cpp src/mavlink_dict.cpp
    src/command_tracker.cpp src/telemetry.cpp src/payload_profile_vio.cpp)
target_include_directories(appcore PUBLIC src)
target_link_libraries(appcore PUBLIC Qt6::Gui mavcore PRIVATE PayloadSDK)
target_compile_options(appcore PRIVATE -Wall -Wextra)
# XML definitions matching the generated headers; see MavlinkDict::defaultDir()
target_compile_definitions(appcore PRIVATE
    MAVLINK_XML_DIR="${SDK_DIR}/libs/third-party/mavlink/include/mavlink/v2.0/message_definitions")

# --- Widgets and the GUI application ---
add_library(appgui STATIC src/main_window.cpp src/inspector_widget.cpp src/stats_widget.cpp src/payload_pages.cpp)
target_link_libraries(appgui PUBLIC Qt6::Widgets appcore)
target_compile_options(appgui PRIVATE -Wall -Wextra)

add_executable(mavlink_debug_app src/main.cpp)
target_link_libraries(mavlink_debug_app PRIVATE appgui)

# --- Tests (no hardware needed) ---
enable_testing()
add_executable(test_mavlink_frame tests/test_mavlink_frame.cpp)
target_link_libraries(test_mavlink_frame mavcore)
add_test(NAME mavlink_frame COMMAND test_mavlink_frame)

add_executable(test_sdk_transport tests/test_sdk_transport.cpp)
target_link_libraries(test_sdk_transport PayloadSDK mavcore)
add_test(NAME sdk_transport COMMAND test_sdk_transport)
set_tests_properties(sdk_transport PROPERTIES TIMEOUT 30)  # a shutdown hang fails instead of blocking

foreach(t link_logic payload_link inspector payload_control main_window)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE Qt6::Test appgui)
    add_test(NAME ${t} COMMAND test_${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 60 ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endforeach()
