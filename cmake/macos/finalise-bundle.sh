#!/bin/bash
#
# Vendor ONNX Runtime into promatte.plugin, make the plugin's reference to it
# resolvable, and seal the bundle.
#
#   usage: finalise-bundle.sh <onnxruntime dylib> <bundle Contents dir> <plugin binary>
#
# This exists because getting it wrong is silent. The upstream macOS tarball
# ships the library as a file called libonnxruntime.dylib whose recorded install
# name is @rpath/libonnxruntime.<version>.dylib, so the name on disk and the name
# the plugin asks dyld for are different strings. Two releases were built by
# deriving the reference from the filename - once from the symlink, once from the
# real path - and both produced a bundle missing the file dyld wanted, because
# install_name_tool -change succeeds and does nothing when its "from" string
# matches no load command. The only reliable source for that string is the dylib
# itself, which is what this script reads, and it then checks the rewrite landed
# rather than assuming it.
set -euo pipefail

src=$1
contents=$2
binary=$3

fw="$contents/Frameworks"
bundle=$(dirname "$contents")
name=$(basename "$src")

mkdir -p "$fw"
cp -f "$src" "$fw/$name"
chmod u+w "$fw/$name"

# otool -D prints a "<path> (architecture <arch>):" label line before each
# slice's install name; skip those and take the first real entry.
old=$(otool -D "$fw/$name" | awk '!/:[[:space:]]*$/ && NF { print; exit }')
if [ -z "$old" ]; then
    echo "could not read the install name of $fw/$name" >&2
    exit 1
fi
new="@loader_path/../Frameworks/$name"
echo "onnxruntime: $old -> $new"

install_name_tool -id "$new" "$fw/$name"
install_name_tool -change "$old" "$new" "$binary"

# The check 1.0.1 and the first attempt at 1.0.2 were both missing.
if otool -L "$binary" | grep -qF "$old"; then
    echo "install_name_tool left $old in $binary" >&2
    otool -L "$binary" >&2
    exit 1
fi
if ! otool -L "$binary" | grep -qF "$new"; then
    echo "$binary does not reference $new" >&2
    otool -L "$binary" >&2
    exit 1
fi

# arm64 code needs a valid signature to be loaded at all, and rewriting load
# commands invalidates whatever the linker produced. There is no signing identity
# here, so seal it ad-hoc; users still clear the quarantine flag by hand.
codesign --force --sign - "$fw/$name"
codesign --force --sign - "$bundle"
codesign --verify --verbose=2 "$bundle"
echo "sealed $bundle"
