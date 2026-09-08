# Locates libobs, ONNX Runtime, nlohmann/json and (Windows) DirectML.
#
# Windows builds against the SDK produced by tools/setup-deps.ps1 and the NuGet
# packages it unpacks into .deps/. Linux and macOS use the system OBS
# development package (pkg-config) and an ONNX Runtime release tarball, because
# neither ships a NuGet layout.
include_guard(GLOBAL)

# DirectML is a Windows-only Microsoft component; make sure the option cannot
# stay on elsewhere, because it also gates PROMATTE_HAVE_DIRECTML in the sources.
if(NOT WIN32 AND PROMATTE_ENABLE_DIRECTML)
    set(PROMATTE_ENABLE_DIRECTML OFF CACHE BOOL "Enable DirectML execution provider (Windows)" FORCE)
endif()

# --- libobs -----------------------------------------------------------------
if(PROMATTE_BUILD_PLUGIN)
    # The SDK in .deps is produced by tools/setup-deps.ps1 and is Windows-only:
    # picking it up elsewhere pulls in w32-pthreads and fails to generate. Only
    # the search path is gated, not find_package itself, because macOS also
    # builds libobs from source and passes its location in CMAKE_PREFIX_PATH.
    if(WIN32)
        set(_obs_sdk "${PROMATTE_DEPS_DIR}/obs-sdk")
        if(EXISTS "${_obs_sdk}/cmake/libobsConfig.cmake")
            list(APPEND CMAKE_PREFIX_PATH "${_obs_sdk}" "${_obs_sdk}/cmake")
        endif()
    endif()
    find_package(libobs QUIET)
    if(NOT TARGET OBS::libobs)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(LIBOBS QUIET IMPORTED_TARGET libobs)
        endif()
        if(TARGET PkgConfig::LIBOBS)
            # ALIAS needs a non-imported target, so wrap it.
            add_library(promatte_libobs INTERFACE)
            target_link_libraries(promatte_libobs INTERFACE PkgConfig::LIBOBS)
            add_library(OBS::libobs ALIAS promatte_libobs)
            message(STATUS "ProMatte: libobs ${LIBOBS_VERSION} (pkg-config)")
        else()
            message(FATAL_ERROR
                "libobs not found.\n"
                "  Windows: run tools/setup-deps.ps1\n"
                "  Linux:   install libobs-dev (apt install libobs-dev)\n"
                "  macOS:   build obs-studio and point CMAKE_PREFIX_PATH at its SDK")
        endif()
    else()
        message(STATUS "ProMatte: libobs found (${libobs_DIR})")
    endif()
endif()

# --- ONNX Runtime -----------------------------------------------------------
# Windows: Microsoft.ML.OnnxRuntime.DirectML NuGet layout.
# Linux/macOS: the onnxruntime-<platform>-<arch>-<version> release tarball, or a
# system install. Set PROMATTE_ORT_ROOT to override.
set(PROMATTE_ORT_ROOT "" CACHE PATH "Root of an ONNX Runtime distribution")
set(_ort_hints
    "${PROMATTE_ORT_ROOT}"
    "${PROMATTE_DEPS_DIR}/onnxruntime"
    "$ENV{ONNXRUNTIME_ROOT}")
file(GLOB _ort_globs
    "${PROMATTE_DEPS_DIR}/onnxruntime-*"
    "/opt/ort/onnxruntime-*"
    "/usr/local/onnxruntime*")
list(APPEND _ort_hints ${_ort_globs})

set(_ort_inc_suffixes build/native/include include include/onnxruntime include/onnxruntime/core/session)
set(_ort_lib_suffixes runtimes/win-x64/native lib lib64)

find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS ${_ort_hints} PATH_SUFFIXES ${_ort_inc_suffixes})
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
    HINTS ${_ort_hints} PATH_SUFFIXES ${_ort_lib_suffixes})
if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR
        "ONNX Runtime not found.\n"
        "  Windows: run tools/setup-deps.ps1\n"
        "  Linux/macOS: unpack an onnxruntime release tarball and pass -DPROMATTE_ORT_ROOT=<dir>")
endif()

add_library(promatte::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(promatte::onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}")
if(WIN32)
    find_file(ONNXRUNTIME_DLL onnxruntime.dll HINTS ${_ort_hints} PATH_SUFFIXES ${_ort_lib_suffixes})
    find_file(ONNXRUNTIME_PROVIDERS_SHARED_DLL onnxruntime_providers_shared.dll
        HINTS ${_ort_hints} PATH_SUFFIXES ${_ort_lib_suffixes})
    set_target_properties(promatte::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_DLL}"
        IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}")
else()
    set_target_properties(promatte::onnxruntime PROPERTIES IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}")
endif()
message(STATUS "ProMatte: ONNX Runtime ${ONNXRUNTIME_LIBRARY}")

# --- DirectML (Windows only) ------------------------------------------------
if(WIN32 AND PROMATTE_ENABLE_DIRECTML)
    find_file(DIRECTML_DLL DirectML.dll HINTS "${PROMATTE_DEPS_DIR}/directml/bin/x64-win")
    find_path(DIRECTML_INCLUDE_DIR DirectML.h HINTS "${PROMATTE_DEPS_DIR}/directml/include")
    if(NOT DIRECTML_DLL)
        message(WARNING "DirectML.dll not found; the DirectML backend will be unavailable at runtime")
    else()
        message(STATUS "ProMatte: DirectML ${DIRECTML_DLL}")
    endif()
endif()

# --- nlohmann/json ----------------------------------------------------------
# Prefer the system copy off Windows; the obs-deps bundle is a Windows artefact.
if(WIN32)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
        HINTS "${PROMATTE_DEPS_DIR}/obs-deps/include" "${PROMATTE_DEPS_DIR}/json/include")
else()
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp HINTS /usr/include /usr/local/include /opt/homebrew/include)
endif()
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp not found (Linux: apt install nlohmann-json3-dev)")
endif()
add_library(promatte::json INTERFACE IMPORTED GLOBAL)
set_target_properties(promatte::json PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${NLOHMANN_JSON_INCLUDE_DIR}")

# --- libcurl (model downloads on Linux/macOS; Windows uses WinHTTP) ---------
if(NOT WIN32)
    find_package(CURL REQUIRED)
    message(STATUS "ProMatte: libcurl ${CURL_VERSION_STRING}")
endif()

# Runtime libraries that must sit next to the plugin (Windows only; on Linux and
# macOS the loader finds them through rpath or the system package).
set(PROMATTE_RUNTIME_DLLS "")
if(WIN32)
    list(APPEND PROMATTE_RUNTIME_DLLS "${ONNXRUNTIME_DLL}")
    if(ONNXRUNTIME_PROVIDERS_SHARED_DLL)
        list(APPEND PROMATTE_RUNTIME_DLLS "${ONNXRUNTIME_PROVIDERS_SHARED_DLL}")
    endif()
    if(DIRECTML_DLL)
        list(APPEND PROMATTE_RUNTIME_DLLS "${DIRECTML_DLL}")
    endif()
endif()
