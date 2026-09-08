# Locates libobs (built SDK), ONNX Runtime and DirectML.
include_guard(GLOBAL)

# --- libobs -----------------------------------------------------------------
set(_obs_sdk "${PROMATTE_DEPS_DIR}/obs-sdk")
if(EXISTS "${_obs_sdk}/cmake/libobsConfig.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${_obs_sdk}" "${_obs_sdk}/cmake")
endif()
if(PROMATTE_BUILD_PLUGIN)
    find_package(libobs REQUIRED)
    message(STATUS "ProMatte: libobs found (${libobs_DIR})")
endif()

# --- ONNX Runtime -----------------------------------------------------------
set(_ort_root "${PROMATTE_DEPS_DIR}/onnxruntime")
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS "${_ort_root}/build/native/include" "${_ort_root}/include")
find_library(ONNXRUNTIME_LIBRARY onnxruntime
    HINTS "${_ort_root}/runtimes/win-x64/native" "${_ort_root}/lib")
find_file(ONNXRUNTIME_DLL onnxruntime.dll
    HINTS "${_ort_root}/runtimes/win-x64/native" "${_ort_root}/lib")
find_file(ONNXRUNTIME_PROVIDERS_SHARED_DLL onnxruntime_providers_shared.dll
    HINTS "${_ort_root}/runtimes/win-x64/native" "${_ort_root}/lib")
if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "ONNX Runtime not found under ${_ort_root}. Run tools/setup-deps.ps1 first.")
endif()
add_library(promatte::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(promatte::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_DLL}"
    IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}")
message(STATUS "ProMatte: ONNX Runtime ${ONNXRUNTIME_DLL}")

# --- DirectML ---------------------------------------------------------------
if(PROMATTE_ENABLE_DIRECTML)
    find_file(DIRECTML_DLL DirectML.dll HINTS "${PROMATTE_DEPS_DIR}/directml/bin/x64-win")
    find_path(DIRECTML_INCLUDE_DIR DirectML.h HINTS "${PROMATTE_DEPS_DIR}/directml/include")
    if(NOT DIRECTML_DLL)
        message(WARNING "DirectML.dll not found; DirectML EP will fail to load at runtime")
    else()
        message(STATUS "ProMatte: DirectML ${DIRECTML_DLL}")
    endif()
endif()

# --- nlohmann/json (header-only, from obs-deps) -----------------------------
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
    HINTS "${PROMATTE_DEPS_DIR}/obs-deps/include" "${PROMATTE_DEPS_DIR}/json/include")
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp not found (expected in obs-deps include dir)")
endif()
add_library(promatte::json INTERFACE IMPORTED GLOBAL)
set_target_properties(promatte::json PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${NLOHMANN_JSON_INCLUDE_DIR}")

set(PROMATTE_RUNTIME_DLLS "${ONNXRUNTIME_DLL}")
if(ONNXRUNTIME_PROVIDERS_SHARED_DLL)
    list(APPEND PROMATTE_RUNTIME_DLLS "${ONNXRUNTIME_PROVIDERS_SHARED_DLL}")
endif()
if(DIRECTML_DLL)
    list(APPEND PROMATTE_RUNTIME_DLLS "${DIRECTML_DLL}")
endif()
