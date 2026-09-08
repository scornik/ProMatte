# Changelog

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
