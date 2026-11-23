cmake_minimum_required(VERSION 3.16)
project(vuprs_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test)

add_library(vuprs_client_core STATIC
    core/frame_protocol.cpp
    core/fibonacci.cpp
    core/waveform_buffer.cpp
    core/app_config.cpp
    net/control_client.cpp
    record/vbf_format.cpp
    record/vbf_reader.cpp
    record/vbf_csv.cpp
    record/recording_naming.cpp
    record/recording_writer.cpp
)
target_include_directories(vuprs_client_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vuprs_client_core PUBLIC Qt6::Core Qt6::Network)
if(NOT MSVC)
    target_compile_options(vuprs_client_core PRIVATE -Wall -Wextra)
endif()

add_executable(vuprs_client
    main.cpp
    ui/main_window.cpp
    ui/settings_dialog.cpp
    ui/time_range_dialog.cpp
    ui/waveform_widget.cpp
    ui/hemisphere_widget.cpp
)
target_link_libraries(vuprs_client PRIVATE vuprs_client_core Qt6::Widgets)
if(WIN32)
    set_target_properties(vuprs_client PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(NOT MSVC)
    target_compile_options(vuprs_client PRIVATE -Wall -Wextra)
endif()

configure_file(doc/client_config.json ${CMAKE_CURRENT_BINARY_DIR}/client_config.json COPYONLY)

include(CTest)
if(BUILD_TESTING)
    add_executable(vuprs_client_tests tests/client_tests.cpp)
    target_link_libraries(vuprs_client_tests PRIVATE vuprs_client_core Qt6::Test)
    if(NOT MSVC)
        target_compile_options(vuprs_client_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME vuprs_client_tests COMMAND vuprs_client_tests)
endif()
