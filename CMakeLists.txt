cmake_minimum_required(VERSION 3.20)
project(LBEE-Decropper LANGUAGES CXX ASM_MASM)

# Force x64
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project must be built for x64 (Kanon is a 64-bit game)")
endif()

# --- MinHook via FetchContent ---
include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

# --- Compile HLSL shaders to .h headers ---
find_program(FXC fxc HINTS
    "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{WindowsSdkDir}/bin/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
)
if(NOT FXC)
    message(FATAL_ERROR "fxc.exe not found. Make sure the Windows SDK is installed.")
endif()

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

add_custom_command(
    OUTPUT ${SHADER_OUT_DIR}/CgGs.h
    COMMAND ${FXC} /nologo /T gs_5_0 /Fh ${SHADER_OUT_DIR}/CgGs.h /Vn g_CgGs /E main ${SHADER_DIR}/CgGs.hlsl
    DEPENDS ${SHADER_DIR}/CgGs.hlsl ${SHADER_DIR}/GS.hlsl ${SHADER_DIR}/SharedInc.h
    COMMENT "Compiling CgGs.hlsl -> CgGs.h"
)

add_custom_command(
    OUTPUT ${SHADER_OUT_DIR}/DecalGs.h
    COMMAND ${FXC} /nologo /T gs_5_0 /Fh ${SHADER_OUT_DIR}/DecalGs.h /Vn g_DecalGs /E main ${SHADER_DIR}/DecalGs.hlsl
    DEPENDS ${SHADER_DIR}/DecalGs.hlsl ${SHADER_DIR}/GS.hlsl ${SHADER_DIR}/SharedInc.h
    COMMENT "Compiling DecalGs.hlsl -> DecalGs.h"
)

add_custom_target(compile_shaders DEPENDS
    ${SHADER_OUT_DIR}/CgGs.h
    ${SHADER_OUT_DIR}/DecalGs.h
)

# --- Main DLL ---
add_library(D3D11 SHARED
    D3D11.cpp
    proxy_x64.asm
)

# Shaders must be compiled before the C++ source
add_dependencies(D3D11 compile_shaders)

# Include the generated shader headers
target_include_directories(D3D11 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHADER_OUT_DIR}
)

# Link MinHook and d3d11 import lib
target_link_libraries(D3D11 PRIVATE minhook d3d11)

# Use the .def file for exports
set_target_properties(D3D11 PROPERTIES
    LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/D3D11.def"
    OUTPUT_NAME "D3D11"
    PREFIX ""
    SUFFIX ".dll"
)

# Install target: copy DLL to game directory
set(KANON_DIR "C:/Program Files (x86)/Steam/steamapps/common/Kanon" CACHE PATH "Kanon game directory")
install(TARGETS D3D11 RUNTIME DESTINATION "${KANON_DIR}")
