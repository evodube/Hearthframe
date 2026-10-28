cmake_minimum_required(VERSION 3.24)
project(FolderGallery VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- CommonLibSSE-NG (git submodule, v3.7.0) ----
# Only the Skyrim SE / AE runtimes; no VR and no unit tests.
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG EXCLUDE_FROM_ALL)
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

# ---- other dependencies (vcpkg) ----
find_package(imgui CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    AUTHOR "evodube"
    USE_ADDRESS_LIBRARY
    SOURCES src/main.cpp src/Gallery.cpp src/Wall.cpp src/Dds.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE src ${STB_INCLUDE_DIRS})
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui d3d11 dxgi)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /permissive- /Zc:preprocessor)
    target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL;/OPT:REF;/OPT:ICF>")
endif()
