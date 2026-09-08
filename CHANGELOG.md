# Changelog

## 1.0.2 — 2026-09-08

**The macOS plugin now links correctly.** 1.0.1 made OBS report *"the following
OBS plugins failed to load: promatte"* on every Mac. Two independent faults were
baked into the shipped binary, either of which was enough on its own:

* The module's only `LC_RPATH` was `@loader_path/../Frameworks`, which points
  inside ProMatte's own bundle. Its dependency on
  `@rpath/libobs.framework/Versions/A/libobs` therefore had nowhere to resolve,
  because libobs lives in `OBS.app/Contents/Frameworks` and the plugin sits
  outside the app. The bundle now also carries `@executable_path/../Frameworks`,
  the same pair the upstream OBS plugin template sets.
* The vendored ONNX Runtime was copied into the bundle under the name of the
  unversioned symlink, `libonnxruntime.dylib`, while the dependency recorded in
  the module was the versioned `@rpath/libonnxruntime.1.23.0.dylib`. The file
  dyld asked for was not in the bundle at all. The `install_name_tool -change`
  that was meant to paper over this used the same wrong name, so it matched no
  load command and did nothing without failing. The real file is now vendored
  under its real name and the reference points straight at it.

**CI now proves the bundle is loadable instead of eyeballing it.**
`tools/check-macos-bundle.py` parses the Mach-O load commands and resolves every
one of them against the bundle: system libraries are accepted, `@loader_path`
dependencies must exist on disk, `@rpath` dependencies must be reachable through
an rpath that is actually present, and anything OBS.app supplies requires an
`@executable_path` rpath. It runs against both the built bundle and the unpacked
archive, and both slices must be universal. The checks it replaces — `otool -L`
piped to `head` and an archive size test — passed cleanly on the broken 1.0.1
build; this script fails it with six errors.

**Confirmed working on Apple Silicon.** The released bundle was installed on an
M-series Mac running OBS 32.2.2: the module loads, the filter attaches to a
camera and *Remove (transparent)* cuts the subject out, with OBS holding
30.00/30.00 fps at 23.6 % CPU. This is the first time ProMatte has been run
inside OBS on a Mac, and it also settles the question the structural checks could
not answer — whether libobs 31.1.1 headers are compatible with the libobs inside
OBS 32.x — for arm64. Intel Macs remain built and structurally verified but never
executed, since both the CI runner and the confirming machine are Apple Silicon.

Windows and Linux are unaffected; nothing in their build changed.

## 1.0.1 — 2026-09-08

macOS now ships as a universal binary, so ProMatte runs on Intel Macs as well as
Apple Silicon, and a use-after-free found while making that work has been fixed.

### Changes since 1.0.0

**macOS supports Intel.** The bundle is a universal binary carrying both an
`x86_64` and an `arm64` slice, so one download works on every supported Mac.
CI asserts with `lipo` that both slices are present in the module and in the
vendored ONNX Runtime, because a package that quietly lost one would still
install and then fail to load on the other kind of Mac. macOS pins ONNX Runtime
1.23.0, the last release with a universal2 build; 1.24 is Apple Silicon only.
Windows and Linux stay on 1.24.4.

**Fixed a use-after-free reading model input shapes.**
`TypeInfo::GetTensorTypeAndShapeInfo()` returns a non-owning view, and it was
being called on a temporary that died at the end of the statement. On ONNX
Runtime 1.24 the dangling read happened to return plausible values, so it went
unnoticed and 1.0.0 shipped with it. On 1.23 the garbage dimension count made
every model fail to load. It is undefined behaviour either way and is now fixed,
verified against both runtimes.

If you are on Windows or Linux, 1.0.0 works and this is a correctness fix rather
than a behaviour change you will notice. If you are on an Intel Mac, this is the
first release you can use at all.

### Downloads

| Platform | File |
| -------- | ---- |
| Windows 10/11 x64 | `ProMatte-Setup-1.0.1.exe` |
| Linux x86_64 (Debian/Ubuntu) | `promatte_1.0.1_amd64.deb` |
| Linux x86_64 (other) | `promatte_1.0.1_linux-x86_64.tar.gz` |
| macOS 11+, Intel and Apple Silicon | `ProMatte-1.0.1-macos-universal.zip` |
| macOS (alternative) | `ProMatte-1.0.1-macos-universal.tar.gz` |

`SHA256SUMS.txt` covers every file. Requires OBS Studio 30.0 or newer.

On macOS, move `promatte.plugin` into
`~/Library/Application Support/obs-studio/plugins`, then clear the quarantine
flag with `xattr -dr com.apple.quarantine` on it, since the bundle is unsigned.

### What is actually verified

| Platform | Builds | Unit tests | Package installs | Runs in OBS with a camera |
| -------- | ------ | ---------- | ---------------- | ------------------------- |
| Windows x64 | yes | 36/36 | yes | **yes**, fully |
| Linux x86_64 | yes | 36/36 | yes, module loads and all libraries resolve | **no** |
| macOS universal | yes, in CI | 36/36 in CI, arm64 slice only | bundle inspected, not installed | **no** |

Windows is the only platform exercised end to end against a real webcam. Linux
builds, passes its tests and produces a package whose module loads, but has
never run inside OBS. macOS is built, tested and packaged by CI and has never
been loaded in OBS on a Mac; the `x86_64` slice in particular is verified
structurally with `lipo`, not executed, because the runner is Apple Silicon.

Full measurements and the complete limitations list are in
[docs/final-verification.md](https://github.com/scornik/ProMatte/blob/v1.0.1/docs/final-verification.md).
Third-party attribution is in
[THIRD_PARTY_LICENSES.md](https://github.com/scornik/ProMatte/blob/v1.0.1/THIRD_PARTY_LICENSES.md).

ProMatte is GPL-2.0-or-later.

## 1.0.0 — 2026-09-08

First release. Windows, Linux and macOS (Apple Silicon) packages:
<https://github.com/scornik/ProMatte/releases/tag/v1.0.0>
