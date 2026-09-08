# Package generation for Linux and macOS.
#
#   Linux: a .deb and a .tar.gz laying the module into <libdir>/obs-plugins and
#          the data into <datadir>/obs/obs-plugins/promatte, which is where the
#          distribution OBS package looks.
#   macOS: a .pkg installing promatte.plugin into
#          /Library/Application Support/obs-studio/plugins, the system-wide
#          location OBS scans.
#
# Windows keeps using the Inno Setup script in installer/, which detects the OBS
# installation directory and removes a stale ProgramData copy; CPack has no
# equivalent for either.
include_guard(GLOBAL)

if(WIN32 OR NOT PROMATTE_BUILD_PLUGIN)
    return()
endif()

set(CPACK_PACKAGE_NAME "promatte")
set(CPACK_PACKAGE_VENDOR "ProMatte")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "ProMatte - real-time AI background removal for OBS Studio")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/scornik/ProMatte")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CONTACT "ProMatte maintainers")
set(CPACK_STRIP_FILES ON)
# One package containing everything; naming a component list as well makes CPack
# warn that the two settings conflict.
set(CPACK_MONOLITHIC_INSTALL ON)

if(APPLE)
    # The bundle is sealed with an ad-hoc signature at the end of the build, and
    # stripping a Mach-O invalidates that. install(DIRECTORY) does not strip, so
    # this changes nothing today, but it keeps a later switch back to
    # install(TARGETS) from quietly producing an unloadable bundle.
    set(CPACK_STRIP_FILES OFF)
    # productbuild shows the licence in the installer and accepts only .rtfd,
    # .rtf, .html or .txt, so give it a copy with an extension it recognises.
    configure_file("${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/LICENSE.txt" COPYONLY)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_BINARY_DIR}/LICENSE.txt")
    # An archive of the .plugin bundle, which is how OBS plugins are normally
    # distributed on macOS: the user drops it into
    # ~/Library/Application Support/obs-studio/plugins.
    #
    # productbuild was tried first and staged the bundle correctly (66 MB), but
    # emitted an 8 KB distribution wrapper with no payload attached, so it would
    # have shipped an installer that installs nothing. ZIP and TGZ both carry the
    # real bundle; TGZ is included because it preserves the executable bit
    # everywhere.
    set(CPACK_GENERATOR "ZIP;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "")
    set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
    # Name by what was actually built: CMAKE_SYSTEM_PROCESSOR is just the host.
    list(LENGTH CMAKE_OSX_ARCHITECTURES _n_archs)
    if(_n_archs GREATER 1)
        set(_arch_tag "universal")
    elseif(CMAKE_OSX_ARCHITECTURES)
        set(_arch_tag "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_arch_tag "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(CPACK_PACKAGE_FILE_NAME "ProMatte-${PROJECT_VERSION}-macos-${_arch_tag}")
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_PACKAGE_FILE_NAME "promatte_${PROJECT_VERSION}_linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ProMatte maintainers")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # obs-studio pulls in libobs; libcurl is used for model downloads. ONNX Runtime
    # is not packaged by Debian or Ubuntu, so it is shipped alongside the module.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "obs-studio (>= 29.0), libcurl4")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
        "Real-time AI background removal, blur and replacement for OBS Studio.\n"
        " Runs entirely on the local machine: no cloud service, no account and no\n"
        " telemetry. Inference runs on a dedicated thread so OBS never stalls, with\n"
        " GPU acceleration where an ONNX Runtime execution provider is available and\n"
        " an automatic CPU fallback.")
    # Ship libonnxruntime next to the module and point the module's RUNPATH at it,
    # since no distribution packages ONNX Runtime.
    get_target_property(_ort promatte::onnxruntime IMPORTED_LOCATION)
    if(_ort)
        get_filename_component(_ort_real "${_ort}" REALPATH)
        get_filename_component(_ort_dir "${_ort}" DIRECTORY)
        file(GLOB _ort_libs "${_ort_dir}/libonnxruntime.so*")
        install(FILES ${_ort_libs}
                DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins/promatte"
                COMPONENT plugin)
        set_target_properties(promatte PROPERTIES
            INSTALL_RPATH "$ORIGIN/promatte"
            BUILD_WITH_INSTALL_RPATH FALSE)
        # The bundled copy is not a system library, so do not let dpkg-shlibdeps
        # turn it into an unsatisfiable dependency.
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    endif()
endif()

include(CPack)
