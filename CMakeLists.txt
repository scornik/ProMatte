cmake_minimum_required(VERSION 3.28)

# ProMatte — Professional Real-Time AI Background Removal for OBS Studio
project(promatte VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(PROMATTE_BUILD_TESTS "Build unit/integration tests" ON)
option(PROMATTE_BUILD_BENCHMARK "Build the benchmark utility" ON)
option(PROMATTE_BUILD_PLUGIN "Build the OBS plugin module (requires libobs)" ON)
option(PROMATTE_ENABLE_DIRECTML "Enable DirectML execution provider (Windows)" ON)
set(PROMATTE_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "Directory containing prebuilt dependencies")
set(PROMATTE_OBS_INSTALL_DIR "" CACHE PATH "Optional: OBS Studio install directory to deploy to (e.g. C:/Program Files/obs-studio)")

include(ProMatteDependencies)   # locates libobs, onnxruntime, directml
include(ProMatteHelpers)

# ---------------------------------------------------------------------------
# Core library: everything that does not depend on libobs (testable, reusable
# by the benchmark tool).
# ---------------------------------------------------------------------------
add_library(promatte-core STATIC
    src/utils/logging.cpp
    src/utils/sha256.cpp
    src/utils/file_utils.cpp
    src/utils/system_info.cpp
    src/inference/model_descriptor.cpp
    src/inference/backend_registry.cpp
    src/inference/inference_worker.cpp
    src/backends/onnxruntime_backend.cpp
    src/preprocessing/preprocess.cpp
    src/postprocessing/matte_refine.cpp
    src/temporal/temporal_stabilizer.cpp
    src/performance/performance_controller.cpp
    src/performance/capability_store.cpp
    src/models/model_manager.cpp
    src/models/model_downloader.cpp
)
target_include_directories(promatte-core PUBLIC src)
target_link_libraries(promatte-core PUBLIC promatte::onnxruntime promatte::json)
target_compile_definitions(promatte-core PUBLIC
    PROMATTE_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${PROMATTE_ENABLE_DIRECTML}>:PROMATTE_HAVE_DIRECTML=1>
    $<$<BOOL:${WIN32}>:NOMINMAX>
    $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>)
promatte_set_warnings(promatte-core)
if(WIN32)
    target_link_libraries(promatte-core PUBLIC dxgi d3d12 winhttp)
    if(DIRECTML_INCLUDE_DIR)
        target_include_directories(promatte-core PUBLIC "${DIRECTML_INCLUDE_DIR}")
    endif()
else()
    # Model downloads use libcurl outside Windows; threads are needed explicitly
    # for the inference worker on Linux.
    find_package(Threads REQUIRED)
    target_link_libraries(promatte-core PUBLIC CURL::libcurl Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# OBS plugin module
# ---------------------------------------------------------------------------
if(PROMATTE_BUILD_PLUGIN)
add_library(promatte MODULE
    src/plugin/plugin-main.cpp
    src/obs/filter.cpp
    src/obs/settings.cpp
    src/obs/presets.cpp
    src/obs/background_source.cpp
    src/ui/properties.cpp
    src/rendering/gpu_pipeline.cpp
    src/rendering/debug_overlay.cpp
)
target_link_libraries(promatte PRIVATE promatte-core OBS::libobs)
target_compile_definitions(promatte PRIVATE PROMATTE_MODULE_NAME="promatte")
promatte_set_warnings(promatte)
set_target_properties(promatte PROPERTIES PREFIX "" OUTPUT_NAME "promatte")

if(WIN32)
    # Stage a ready-to-install plugin tree in build/stage/ (obs-plugins/64bit + data)
    promatte_stage_plugin(promatte)
else()
    promatte_install_plugin(promatte)
endif()
endif()

# ---------------------------------------------------------------------------
# Tools & tests
# ---------------------------------------------------------------------------
if(PROMATTE_BUILD_BENCHMARK)
    add_subdirectory(tools/benchmark)
endif()
if(PROMATTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Linux .deb/.tar.gz and macOS .pkg. Windows uses installer/ProMatte.iss instead.
include(ProMattePackaging)
