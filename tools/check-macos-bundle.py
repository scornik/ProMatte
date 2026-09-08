#!/usr/bin/env python3
"""Verify that promatte.plugin can actually be loaded by OBS on macOS.

Written after 1.0.1 shipped a bundle that OBS refused to load. Two independent
faults were baked into the binary, and neither was visible in the checks the CI
job was doing (``otool -L | head`` and an archive size test):

  * the only ``LC_RPATH`` was ``@loader_path/../Frameworks``, so the plugin's
    ``@rpath/libobs.framework/Versions/A/libobs`` had nowhere to resolve once the
    bundle sat outside OBS.app;
  * the vendored ONNX Runtime was copied in under the name of the unversioned
    symlink while the recorded dependency was the versioned one, so the file dyld
    asked for was not in the bundle at all.

Both are structural and can be proven from the Mach-O headers without a Mac,
which is what this script does. It parses the load commands itself rather than
shelling out to otool so it also runs on the machine that cuts the release.
"""

import struct
import sys
from pathlib import Path

FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF
MH_MAGIC_64 = 0xFEEDFACF
LC_LOAD_DYLIB = 0x0C
LC_ID_DYLIB = 0x0D
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_RPATH = 0x8000001C

CPU_NAMES = {0x0100000C: "arm64", 0x01000007: "x86_64"}

# Resolved out of the dyld shared cache; the files do not exist on disk on
# modern macOS, so their absence is not a fault.
SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")

# Supplied by OBS.app itself rather than by the bundle.
HOST_PROVIDED = ("libobs.framework/",)


class MachOError(Exception):
    pass


def _slices(data):
    """Yield (arch_name, offset) for every Mach-O image in the file."""
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic in (FAT_MAGIC, FAT_MAGIC_64):
        wide = magic == FAT_MAGIC_64
        count = struct.unpack_from(">I", data, 4)[0]
        entry = 32 if wide else 20
        for i in range(count):
            base = 8 + i * entry
            cputype = struct.unpack_from(">I", data, base)[0]
            offset = (struct.unpack_from(">Q", data, base + 8)[0] if wide
                      else struct.unpack_from(">I", data, base + 8)[0])
            yield CPU_NAMES.get(cputype, hex(cputype)), offset
        return
    if struct.unpack_from("<I", data, 0)[0] == MH_MAGIC_64:
        cputype = struct.unpack_from("<I", data, 4)[0]
        yield CPU_NAMES.get(cputype, hex(cputype)), 0
        return
    raise MachOError("not a 64-bit Mach-O or fat binary")


def read_image(path):
    """Return {arch: {"rpaths": [...], "dylibs": [...], "id": str|None}}."""
    data = path.read_bytes()
    out = {}
    for arch, offset in _slices(data):
        if struct.unpack_from("<I", data, offset)[0] != MH_MAGIC_64:
            raise MachOError(f"{path}: {arch} slice is not 64-bit little-endian")
        ncmds = struct.unpack_from("<I", data, offset + 16)[0]
        pos = offset + 32
        info = {"rpaths": [], "dylibs": [], "id": None}
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", data, pos)
            if cmdsize == 0:
                raise MachOError(f"{path}: zero-length load command")
            if cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_ID_DYLIB, LC_RPATH):
                str_off = struct.unpack_from("<I", data, pos + 8)[0]
                name = data[pos + str_off:pos + cmdsize].split(b"\0")[0].decode()
                if cmd == LC_RPATH:
                    info["rpaths"].append(name)
                elif cmd == LC_ID_DYLIB:
                    info["id"] = name
                else:
                    info["dylibs"].append(name)
            pos += cmdsize
        out[arch] = info
    return out


def resolve(dep, rpaths, loader_dir, bundle):
    """Say whether one dependency can be found, and explain it if it cannot."""
    if dep.startswith(SYSTEM_PREFIXES):
        return True, "system library"
    if dep.startswith("@loader_path/"):
        target = (loader_dir / dep[len("@loader_path/"):]).resolve()
        if target.exists():
            return True, f"bundled at {target.relative_to(bundle.parent)}"
        return False, f"{dep} does not exist ({target} is missing)"
    if dep.startswith("@executable_path/"):
        return True, "resolved inside OBS.app at runtime"
    if dep.startswith("@rpath/"):
        tail = dep[len("@rpath/"):]
        if tail.startswith(HOST_PROVIDED):
            for rp in rpaths:
                if rp.startswith("@executable_path/"):
                    return True, f"provided by OBS.app via {rp}"
            return False, (f"{dep} is provided by OBS.app but no LC_RPATH starts "
                           f"with @executable_path/ (rpaths: {rpaths or 'none'})")
        tried = []
        for rp in rpaths:
            if rp.startswith("@loader_path/"):
                cand = (loader_dir / rp[len("@loader_path/"):] / tail).resolve()
            elif rp.startswith("@executable_path/"):
                continue  # inside OBS.app; not a file this bundle ships
            else:
                cand = Path(rp) / tail
            tried.append(str(cand))
            if cand.exists():
                return True, f"bundled at {cand.relative_to(bundle.parent)}"
        return False, (f"{dep} not found under any LC_RPATH inside the bundle; "
                       f"tried {tried or 'nothing (no usable LC_RPATH)'}")
    if dep.startswith("/"):
        return Path(dep).exists(), f"absolute path {dep}"
    return False, f"unrecognised dependency form: {dep}"


def main(argv):
    if len(argv) != 2:
        print("usage: check-macos-bundle.py path/to/promatte.plugin", file=sys.stderr)
        return 2
    bundle = Path(argv[1]).resolve()
    contents = bundle / "Contents"
    binary = contents / "MacOS" / "promatte"
    failures = []

    def check(ok, message):
        print(("  ok    " if ok else "  FAIL  ") + message)
        if not ok:
            failures.append(message)

    print(f"checking {bundle}")

    for required in (contents / "Info.plist", binary,
                     contents / "Resources" / "effects",
                     contents / "Resources" / "locale",
                     contents / "Resources" / "models"):
        check(required.exists(), f"{required.relative_to(bundle)} exists")
    if not binary.exists():
        return 1

    models = list((contents / "Resources" / "models").glob("*.onnx"))
    check(bool(models), f"bundled models present ({len(models)} found)")

    image = read_image(binary)
    check(set(image) >= {"arm64", "x86_64"},
          f"module is universal (slices: {', '.join(sorted(image))})")

    # OBS.app is the executable and the plugin lives outside it, so the binary's
    # own directory is what @loader_path means here.
    loader_dir = binary.parent
    for arch, info in sorted(image.items()):
        print(f"[{arch}]")
        check(any(r.startswith("@executable_path/") for r in info["rpaths"]),
              f"{arch}: has an @executable_path LC_RPATH "
              f"(found: {', '.join(info['rpaths']) or 'none'})")
        for dep in info["dylibs"]:
            ok, why = resolve(dep, info["rpaths"], loader_dir, bundle)
            check(ok, f"{arch}: {dep} -> {why}")

    fw = contents / "Frameworks"
    if fw.is_dir():
        for lib in sorted(fw.glob("*.dylib")):
            libimage = read_image(lib)
            check(set(libimage) >= {"arm64", "x86_64"},
                  f"{lib.name} is universal (slices: {', '.join(sorted(libimage))})")

    print()
    if failures:
        print(f"{len(failures)} problem(s); this bundle would not load in OBS")
        return 1
    print("bundle looks loadable")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
