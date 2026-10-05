cmake_minimum_required(VERSION 3.25)
project(RenegadeVR VERSION 0.0.1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "RenegadeVR currently targets Windows only.")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RenegadeVR must currently be built as Win32/x86.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
include(cmake/RenegadeVRCommon.cmake)

add_library(RenegadeVR SHARED
    src/core/RenegadeVR.cpp
    src/core/Logger.cpp
    src/core/VRCore.cpp
    src/core/VRCamera.cpp
    src/core/VRInput.cpp
    src/core/VRWeapon.cpp
    src/core/VRHands.cpp
)
target_include_directories(RenegadeVR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(RenegadeVR PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX RENEGADEVR_BUILD_DLL)
renegadevr_apply_common_settings(RenegadeVR)

add_library(D3D8Proxy SHARED
    src/d3d8_proxy/D3D8Proxy.cpp
    src/d3d8_proxy/D3D8Hooks.cpp
    src/d3d8_proxy/D3D8Exports.def
)
set_target_properties(D3D8Proxy PROPERTIES OUTPUT_NAME "d3d8")
target_include_directories(D3D8Proxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(D3D8Proxy PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(D3D8Proxy PRIVATE user32)
renegadevr_apply_common_settings(D3D8Proxy)

add_custom_command(TARGET RenegadeVR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/RenegadeVR.ini
        $<TARGET_FILE_DIR:RenegadeVR>/RenegadeVR.ini
)
