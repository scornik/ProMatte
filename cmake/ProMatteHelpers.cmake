include_guard(GLOBAL)

function(promatte_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc /MP
            /wd4324  # structure padded due to alignment
        )
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Stages the plugin into <build>/stage/{obs-plugins/64bit,data/obs-plugins/promatte}
# which mirrors the OBS install layout and is what the installer packages.
function(promatte_stage_plugin target)
    set(stage "${CMAKE_BINARY_DIR}/stage")
    set(bin_dir "${stage}/obs-plugins/64bit")
    set(data_dir "${stage}/data/obs-plugins/promatte")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${bin_dir}" "${data_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${bin_dir}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROMATTE_RUNTIME_DLLS} "${bin_dir}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${data_dir}"
        COMMENT "Staging ProMatte plugin into ${stage}"
        COMMAND_EXPAND_LISTS)
    if(MSVC)
        # Always produce a PDB (also in Release) so crash reports are symbolised.
        target_compile_options(${target} PRIVATE /Zi)
        target_link_options(${target} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${target}>" "${bin_dir}/"
            COMMAND_EXPAND_LISTS)
    endif()
    # Bundled models (small, permissively licensed) ship inside the data directory.
    # models/converted is produced by tools/models/convert_models.py.
    set(_bundled_models
        mediapipe_selfie_landscape_144x256.onnx
        mediapipe_selfie_general_256.onnx
        mediapipe_selfie_multiclass_256.onnx
        pphumanseg_v2_lite_192.onnx
        pphumanseg_v2_portrait_256x144.onnx)
    foreach(_m IN LISTS _bundled_models)
        if(EXISTS "${CMAKE_SOURCE_DIR}/models/converted/${_m}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/models/converted/${_m}" "${data_dir}/models/${_m}")
        else()
            message(WARNING "bundled model missing: models/converted/${_m} (run tools/models/convert_models.py)")
        endif()
    endforeach()
    if(PROMATTE_OBS_INSTALL_DIR)
        add_custom_target(deploy
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage}" "${PROMATTE_OBS_INSTALL_DIR}"
            DEPENDS ${target}
            COMMENT "Deploying staged plugin into ${PROMATTE_OBS_INSTALL_DIR}")
    endif()
    # Per-machine plugin directory (no admin rights needed): %ProgramData%/obs-studio/plugins/promatte
    if(WIN32)
        set(_user_plugins "$ENV{ProgramData}/obs-studio/plugins/promatte")
        add_custom_target(deploy-user
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_user_plugins}/bin/64bit" "${_user_plugins}/data"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${bin_dir}" "${_user_plugins}/bin/64bit"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${data_dir}" "${_user_plugins}/data"
            DEPENDS ${target}
            COMMENT "Deploying staged plugin into ${_user_plugins}")
    endif()
endfunction()
