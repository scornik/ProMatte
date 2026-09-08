# This file will be configured to contain variables for CPack. These variables
# should be set in the CMake list file of the project before CPack module is
# included. The list of available CPACK_xxx variables and their associated
# documentation may be obtained using
#  cpack --help-variable-list
#
# Some variables are common to all generators (e.g. CPACK_PACKAGE_NAME)
# and some are specific to a generator
# (e.g. CPACK_NSIS_EXTRA_INSTALL_COMMANDS). The generator specific variables
# usually begin with CPACK_<GENNAME>_xxxx.


set(CPACK_BUILD_SOURCE_DIRS "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover;/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux")
set(CPACK_CMAKE_GENERATOR "Ninja")
set(CPACK_COMPONENT_UNSPECIFIED_HIDDEN "TRUE")
set(CPACK_COMPONENT_UNSPECIFIED_REQUIRED "TRUE")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "obs-studio (>= 29.0), libcurl4")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Real-time AI background removal, blur and replacement for OBS Studio.
; Runs entirely on the local machine: no cloud service, no account and no
; telemetry. Inference runs on a dedicated thread so OBS never stalls, with
; GPU acceleration where an ONNX Runtime execution provider is available and
; an automatic CPU fallback.")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/scornik/ProMatte")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ProMatte maintainers")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS "OFF")
set(CPACK_DEB_COMPONENT_INSTALL "OFF")
set(CPACK_DEFAULT_PACKAGE_DESCRIPTION_FILE "/usr/share/cmake-3.28/Templates/CPack.GenericDescription.txt")
set(CPACK_DEFAULT_PACKAGE_DESCRIPTION_SUMMARY "promatte built using CMake")
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_INNOSETUP_ARCHITECTURE "x64")
set(CPACK_INSTALL_CMAKE_PROJECTS "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux;promatte;ALL;/")
set(CPACK_INSTALL_PREFIX "/usr/local")
set(CPACK_MODULE_PATH "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/cmake")
set(CPACK_MONOLITHIC_INSTALL "ON")
set(CPACK_NSIS_DISPLAY_NAME "promatte 1.0.0")
set(CPACK_NSIS_INSTALLER_ICON_CODE "")
set(CPACK_NSIS_INSTALLER_MUI_ICON_CODE "")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
set(CPACK_NSIS_PACKAGE_NAME "promatte 1.0.0")
set(CPACK_NSIS_UNINSTALL_NAME "Uninstall")
set(CPACK_OBJCOPY_EXECUTABLE "/usr/bin/objcopy")
set(CPACK_OBJDUMP_EXECUTABLE "/usr/bin/objdump")
set(CPACK_OUTPUT_CONFIG_FILE "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux/CPackConfig.cmake")
set(CPACK_PACKAGE_CONTACT "ProMatte maintainers")
set(CPACK_PACKAGE_DEFAULT_LOCATION "/")
set(CPACK_PACKAGE_DESCRIPTION_FILE "/usr/share/cmake-3.28/Templates/CPack.GenericDescription.txt")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ProMatte - real-time AI background removal for OBS Studio")
set(CPACK_PACKAGE_FILE_NAME "promatte_1.0.0_linux-x86_64")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/scornik/ProMatte")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "promatte 1.0.0")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "promatte 1.0.0")
set(CPACK_PACKAGE_NAME "promatte")
set(CPACK_PACKAGE_RELOCATABLE "true")
set(CPACK_PACKAGE_VENDOR "ProMatte")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_READELF_EXECUTABLE "/usr/bin/readelf")
set(CPACK_RESOURCE_FILE_LICENSE "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/LICENSE")
set(CPACK_RESOURCE_FILE_README "/usr/share/cmake-3.28/Templates/CPack.GenericDescription.txt")
set(CPACK_RESOURCE_FILE_WELCOME "/usr/share/cmake-3.28/Templates/CPack.GenericWelcome.txt")
set(CPACK_SET_DESTDIR "OFF")
set(CPACK_SOURCE_GENERATOR "TBZ2;TGZ;TXZ;TZ")
set(CPACK_SOURCE_OUTPUT_CONFIG_FILE "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux/CPackSourceConfig.cmake")
set(CPACK_SOURCE_RPM "OFF")
set(CPACK_SOURCE_TBZ2 "ON")
set(CPACK_SOURCE_TGZ "ON")
set(CPACK_SOURCE_TXZ "ON")
set(CPACK_SOURCE_TZ "ON")
set(CPACK_SOURCE_ZIP "OFF")
set(CPACK_STRIP_FILES "ON")
set(CPACK_SYSTEM_NAME "Linux")
set(CPACK_THREADS "1")
set(CPACK_TOPLEVEL_TAG "Linux")
set(CPACK_WIX_SIZEOF_VOID_P "8")

if(NOT CPACK_PROPERTIES_FILE)
  set(CPACK_PROPERTIES_FILE "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux/CPackProperties.cmake")
endif()

if(EXISTS ${CPACK_PROPERTIES_FILE})
  include(${CPACK_PROPERTIES_FILE})
endif()
