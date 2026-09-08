add_executable(promatte-unit-tests
    unit/main.cpp
    unit/test_matte_refine.cpp
    unit/test_temporal.cpp
    unit/test_preprocess.cpp
    unit/test_model_descriptor.cpp
    unit/test_model_manager.cpp
    unit/test_performance_controller.cpp
    unit/test_inference_worker.cpp
    unit/test_sha256.cpp
)
target_include_directories(promatte-unit-tests PRIVATE third_party)
target_link_libraries(promatte-unit-tests PRIVATE promatte-core promatte::json)
target_compile_definitions(promatte-unit-tests PRIVATE
    PROMATTE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    PROMATTE_MODELS_DIR="${CMAKE_SOURCE_DIR}/models/converted")
promatte_set_warnings(promatte-unit-tests)
set_target_properties(promatte-unit-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# Windows needs onnxruntime/DirectML beside the executable; elsewhere the
# loader finds them through rpath, and the list is empty.
if(PROMATTE_RUNTIME_DLLS)
    add_custom_command(TARGET promatte-unit-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROMATTE_RUNTIME_DLLS} "$<TARGET_FILE_DIR:promatte-unit-tests>"
        COMMAND_EXPAND_LISTS)
endif()
add_test(NAME unit COMMAND promatte-unit-tests)

# Integration harness: boots libobs headlessly, loads the plugin module, creates
# the filter on a synthetic source and renders frames.
#
# Windows only for now: it drives the D3D11 renderer and loads libobs from the
# SDK built by tools/setup-deps.ps1. On Linux and macOS libobs needs a real
# display and GPU context, which CI runners do not have, so those platforms run
# the unit suite and leave rendering to the live checks in tests/integration/.
if(PROMATTE_BUILD_PLUGIN AND WIN32)
add_executable(promatte-integration-tests
    integration/obs_harness.cpp
)
target_include_directories(promatte-integration-tests PRIVATE third_party)
target_link_libraries(promatte-integration-tests PRIVATE promatte-core OBS::libobs)
target_compile_definitions(promatte-integration-tests PRIVATE
    PROMATTE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    PROMATTE_STAGE_DIR="${CMAKE_BINARY_DIR}/stage"
    PROMATTE_OBS_SDK_DIR="${PROMATTE_DEPS_DIR}/obs-sdk"
    PROMATTE_OBS_BUILD_DIR="${PROMATTE_DEPS_DIR}/obs-studio-31.1.1/build_x64")
promatte_set_warnings(promatte-integration-tests)
set_target_properties(promatte-integration-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_dependencies(promatte-integration-tests promatte)
# libobs + its runtime dependencies must sit next to the test executable.
file(GLOB _obs_runtime_dlls "${PROMATTE_DEPS_DIR}/obs-sdk/bin/64bit/*.dll" "${PROMATTE_DEPS_DIR}/obs-deps/bin/*.dll")
add_custom_command(TARGET promatte-integration-tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_obs_runtime_dlls} ${PROMATTE_RUNTIME_DLLS} "$<TARGET_FILE_DIR:promatte-integration-tests>"
    COMMAND_EXPAND_LISTS)
add_test(NAME integration COMMAND promatte-integration-tests)
endif()
