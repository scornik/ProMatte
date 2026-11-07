# OBS Studio 33 compatibility

OBS 33 removes the legacy `gs_image_file*` exports used by ProMatte 1.0.2.
On Windows this causes LoadLibrary to fail with error 127 before the plugin
can write its startup log. Reinstalling runtime DLLs does not resolve it.

The background-image implementation now selects `gs_image_file_ex_t` and the
`gs_image_file_ex_*` functions when compiled with OBS 33 headers. Builds made
with OBS 30–32 headers retain the original `gs_image_file4_t` implementation.
The structure layout changed too, so renaming DLL imports is insufficient.

Build against the SDK matching the target OBS major version. The existing
31.1.1 dependency default produces an OBS 30–32 build; it does not produce an
OBS 33-compatible binary. Distribute separate binaries for OBS 33 and older OBS.
For a standard Windows OBS 33 SDK build, run setup-deps.ps1 with
`-ObsVersion 33.0.1` and an obs-deps version matching OBS 33, then configure
CMake using that SDK. Use a fresh build/dependency directory when switching SDKs.

The Windows 1.0.3 release is built for OBS 33.0.1 and was verified against
that installed runtime: DLL load succeeds, all 36 unit tests (269 assertions)
pass, and three integration tests (9 assertions) pass. These cover module
registration, filter creation/render/destruction, and background-mode changes.
CPU and DirectML inference both initialize. The released installer includes
models and runtime dependencies. OBS 30–32, Linux, macOS, and a live webcam
session have not been retested for this release.
