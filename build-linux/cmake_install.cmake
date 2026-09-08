# Install script for directory: /mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/obs-plugins/promatte.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/obs-plugins/promatte.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/obs-plugins/promatte.so"
         RPATH "\$ORIGIN/promatte")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/obs-plugins" TYPE MODULE FILES "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux/promatte.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/obs-plugins/promatte.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/obs-plugins/promatte.so")
    file(RPATH_CHANGE
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/obs-plugins/promatte.so"
         OLD_RPATH "/opt/ort/onnxruntime-linux-x64-1.24.4/lib:"
         NEW_RPATH "\$ORIGIN/promatte")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/obs-plugins/promatte.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/promatte" TYPE DIRECTORY FILES "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/data/")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/promatte/models" TYPE FILE FILES "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/models/converted/mediapipe_selfie_landscape_144x256.onnx")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/promatte/models" TYPE FILE FILES "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/models/converted/mediapipe_selfie_general_256.onnx")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/promatte/models" TYPE FILE FILES "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/models/converted/mediapipe_selfie_multiclass_256.onnx")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/promatte/models" TYPE FILE FILES "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/models/converted/pphumanseg_v2_lite_192.onnx")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/promatte/models" TYPE FILE FILES "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/models/converted/pphumanseg_v2_portrait_256x144.onnx")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/promatte" TYPE FILE FILES
    "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/LICENSE"
    "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/THIRD_PARTY_LICENSES.md"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux/tools/benchmark/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux/tests/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "plugin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/obs-plugins/promatte" TYPE FILE FILES
    "/opt/ort/onnxruntime-linux-x64-1.24.4/lib/libonnxruntime.so"
    "/opt/ort/onnxruntime-linux-x64-1.24.4/lib/libonnxruntime.so.1"
    "/opt/ort/onnxruntime-linux-x64-1.24.4/lib/libonnxruntime.so.1.24.4"
    )
endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/mnt/d/Hakeemify/Customer Analysis/OBS Plugin/Background Remover/build-linux/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
