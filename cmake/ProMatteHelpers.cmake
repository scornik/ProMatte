include_guard(GLOBAL)

# Models small enough and permissively enough licensed to ship with the plugin.
# models/converted is produced by tools/models/convert_models.py.
set(PROMATTE_BUNDLED_MODELS
    mediapipe_selfie_landscape_144x256.onnx
    mediapipe_selfie_general_256.onnx
    mediapipe_selfie_multiclass_256.onnx
    pphumanseg_v2_lite_192.onnx
    pphumanseg_v2_portrait_256x144.onnx)

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
    foreach(_m IN LISTS PROMATTE_BUNDLED_MODELS)
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

# Install rules for Linux and macOS.
#
#   Linux: OBS looks in <libdir>/obs-plugins for the module and in
#          <datadir>/obs/obs-plugins/<name> for its data. That is what the .deb
#          and the tarball lay down.
#   macOS: OBS loads a bundle, promatte.plugin, with the binary in
#          Contents/MacOS and the data in Contents/Resources. ONNX Runtime is
#          copied into Contents/Frameworks and the module is relinked against
#          @loader_path so the bundle is self-contained.
function(promatte_install_plugin target)
    include(GNUInstallDirs)
    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "plugin"
            OUTPUT_NAME "promatte"
            MACOSX_BUNDLE_BUNDLE_NAME "ProMatte"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.promatte.obs-plugin"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/macos/Info.plist.in"
            BUILD_WITH_INSTALL_RPATH TRUE
            # @executable_path is OBS.app/Contents/MacOS, so this reaches
            # OBS.app/Contents/Frameworks where libobs.framework lives. Without it
            # the plugin's own @rpath/libobs.framework/... reference cannot resolve
            # once the bundle sits in ~/Library/Application Support/obs-studio/plugins
            # and OBS reports "failed to load". @loader_path/../Frameworks is for the
            # ONNX Runtime vendored inside this bundle. This is the same pair the
            # upstream obs-plugintemplate sets.
            INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks")
        set(_res "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources")
        set(_fw "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_res}" "${_fw}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${_res}"
            COMMENT "Filling the ProMatte bundle")
        foreach(_m IN LISTS PROMATTE_BUNDLED_MODELS)
            if(EXISTS "${CMAKE_SOURCE_DIR}/models/converted/${_m}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/models/converted/${_m}" "${_res}/models/${_m}")
            endif()
        endforeach()
        # Vendor libonnxruntime so the plugin does not depend on a Homebrew prefix.
        get_target_property(_ort promatte::onnxruntime IMPORTED_LOCATION)
        if(_ort)
            # IMPORTED_LOCATION is the unversioned symlink, libonnxruntime.dylib,
            # but the dylib's own install name - and therefore the LC_LOAD_DYLIB the
            # linker records in the plugin - is the versioned
            # @rpath/libonnxruntime.<version>.dylib. Vendoring under the symlink's
            # name left the bundle without the file dyld actually asks for, and the
            # -change below silently did nothing because its "from" string matched no
            # load command. Copy the real file under its real name.
            get_filename_component(_ort_real "${_ort}" REALPATH)
            get_filename_component(_ort_soname "${_ort_real}" NAME)
            get_filename_component(_ort_link "${_ort}" NAME)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_ort_real}" "${_fw}/${_ort_soname}"
                COMMENT "Vendoring ${_ort_soname} into the bundle")
            # Point the reference straight at the bundled copy so it resolves without
            # depending on the rpath at all. Both spellings are rewritten because
            # which one was recorded depends on how the tarball was laid out.
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND install_name_tool -change "@rpath/${_ort_soname}"
                        "@loader_path/../Frameworks/${_ort_soname}" "$<TARGET_FILE:${target}>")
            if(NOT _ort_link STREQUAL _ort_soname)
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND install_name_tool -change "@rpath/${_ort_link}"
                            "@loader_path/../Frameworks/${_ort_soname}" "$<TARGET_FILE:${target}>")
            endif()
        endif()
        # install(TARGETS) copies only the module binary out of the bundle, which
        # produced an 8 KB .pkg with no Resources and no Frameworks. Install the
        # built bundle directory instead so the models, effects, locale and the
        # vendored ONNX Runtime all travel with it.
        install(DIRECTORY "${CMAKE_BINARY_DIR}/promatte.plugin"
                DESTINATION "." USE_SOURCE_PERMISSIONS)
    else()
        install(TARGETS ${target} LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins" COMPONENT plugin)
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/promatte"
                COMPONENT plugin)
        foreach(_m IN LISTS PROMATTE_BUNDLED_MODELS)
            if(EXISTS "${CMAKE_SOURCE_DIR}/models/converted/${_m}")
                install(FILES "${CMAKE_SOURCE_DIR}/models/converted/${_m}"
                        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/promatte/models"
                        COMPONENT plugin)
            endif()
        endforeach()
        install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES.md"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/promatte" COMPONENT plugin)
        # Also drop the data next to the module for a self-contained tarball install.
        set(stage "${CMAKE_BINARY_DIR}/stage")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${stage}/bin/64bit" "${stage}/data"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${stage}/bin/64bit/"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${stage}/data"
            COMMENT "Staging ProMatte plugin into ${stage}")
        foreach(_m IN LISTS PROMATTE_BUNDLED_MODELS)
            if(EXISTS "${CMAKE_SOURCE_DIR}/models/converted/${_m}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/models/converted/${_m}" "${stage}/data/models/${_m}")
            endif()
        endforeach()
    endif()
endfunction()
