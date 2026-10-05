cmake_minimum_required(VERSION 3.25)
project(RenegadeVRXRHost VERSION 0.0.1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "RenegadeVR xrhost currently targets Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RenegadeVR xrhost must be built as x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenXR CONFIG REQUIRED)

add_executable(RenegadeVRXRHost
    ../src/xrhost/XRHostMain.cpp
)

set_target_properties(RenegadeVRXRHost PROPERTIES
    OUTPUT_NAME "RenegadeVR-xrhost"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(RenegadeVRXRHost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_definitions(RenegadeVRXRHost PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    XR_USE_PLATFORM_WIN32
    XR_USE_GRAPHICS_API_D3D11
)

target_link_libraries(RenegadeVRXRHost PRIVATE
    OpenXR::openxr_loader
    d3d11
    dxgi
)
