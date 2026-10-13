#!/usr/bin/env bash
# Package a built qinta as an AppImage: qinta, the Qt libraries and plugins it loads, and a
# desktop entry and icon, in one file that runs on a Linux desktop with no Qt installed.
#
#   scripts/package-linux.sh <build directory> <output directory>
#
# linuxdeploy and its Qt plugin are downloaded at pinned releases and checked against pinned
# hashes, so the packaging cannot change without a commit here. Writes
# <output directory>/qinta-<version>-x86_64.AppImage, and leaves the AppDir beside it for
# inspection. Needs qmake6 (Ubuntu's qt6-base-dev-tools) and python3 with PIL for the icon.
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <build directory> <output directory>" >&2
    exit 2
fi
build=$(cd "$1" && pwd)
mkdir -p "$2"
out=$(cd "$2" && pwd)
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

if [ ! -x "$build/qinta" ]; then
    echo "package-linux: no qinta executable in $build" >&2
    exit 2
fi

LINUXDEPLOY_TAG=1-alpha-20251107-1
LINUXDEPLOY_SHA256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
PLUGIN_QT_TAG=1-alpha-20250213-1
PLUGIN_QT_SHA256=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724

tools="$out/tools"
mkdir -p "$tools"
fetch() {  # url, file, sha256
    if [ ! -f "$tools/$2" ]; then
        curl -sSfL -o "$tools/$2" "$1"
    fi
    echo "$3  $tools/$2" | sha256sum -c --quiet - || {
        echo "package-linux: $2 does not match its pinned hash" >&2
        exit 1
    }
    chmod +x "$tools/$2"
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-x86_64.AppImage" \
    linuxdeploy-x86_64.AppImage "$LINUXDEPLOY_SHA256"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$PLUGIN_QT_TAG/linuxdeploy-plugin-qt-x86_64.AppImage" \
    linuxdeploy-plugin-qt-x86_64.AppImage "$PLUGIN_QT_SHA256"

# The icon: the largest image in resources/qinta.ico, at 256x256, the size desktop entries use.
icon="$out/qinta.png"
python3 - "$root/resources/qinta.ico" "$icon" <<'EOF'
import sys
from PIL import Image
with Image.open(sys.argv[1]) as ico:
    largest = max(ico.ico.sizes())
    ico.size = largest
    ico.load()
    ico.convert("RGBA").resize((256, 256), Image.LANCZOS).save(sys.argv[2])
EOF

# A tagged commit is named by its tag; anything else by its commit, so a snapshot can never be
# mistaken for a release. (CI checks out without tags, so describe would differ from a local run.)
# QINTA_VERSION, when set, names a release build (build.yml sets it from the release input).
version=${QINTA_VERSION:-$(git -C "$root" describe --tags --exact-match 2>/dev/null || git -C "$root" rev-parse --short HEAD)}
appdir="$out/AppDir"
rm -rf "$appdir" "$out"/qinta-*.AppImage

# qmake tells the Qt plugin where this Qt's plugins live. The offscreen platform plugin is
# deployed beside the default xcb one so the AppImage can run headless (--printpages in CI).
export QMAKE=${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake)}
export EXTRA_PLATFORM_PLUGINS=libqoffscreen.so
# No FUSE on CI runners or in containers; the tools extract themselves instead.
export APPIMAGE_EXTRACT_AND_RUN=1
export LDAI_OUTPUT="qinta-$version-x86_64.AppImage"
export OUTPUT="$LDAI_OUTPUT"

# The .mmd MIME type travels in the AppDir, where a desktop integrator (or a person running
# xdg-mime install on it) finds it beside the desktop entry that names it. linuxdeploy keeps
# what is already in the AppDir.
mkdir -p "$appdir/usr/share/mime/packages"
cp "$root/packaging/linux/qinta-mermaid.xml" "$appdir/usr/share/mime/packages/qinta-mermaid.xml"

cd "$out"
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
    --executable "$build/qinta" \
    --desktop-file "$root/packaging/linux/qinta.desktop" \
    --icon-file "$icon" \
    --plugin qt \
    --output appimage

if [ ! -f "$out/$OUTPUT" ]; then
    echo "package-linux: linuxdeploy finished without writing $OUTPUT" >&2
    exit 1
fi

# Self-contained means every Qt library qinta loads resolves inside the AppDir. The count of
# Qt libraries found at all is checked too, so an ldd that printed nothing cannot read as clean.
qt_libs=$(ldd "$appdir/usr/bin/qinta" | grep -c 'libQt6' || true)
outside=$(ldd "$appdir/usr/bin/qinta" | grep 'libQt6' | grep -vc "$appdir" || true)
if [ "$qt_libs" -lt 6 ] || [ "$outside" -ne 0 ]; then
    echo "package-linux: $qt_libs Qt libraries linked, $outside of them from outside the AppDir" >&2
    exit 1
fi
if [ ! -f "$appdir/usr/share/mime/packages/qinta-mermaid.xml" ]; then
    echo "package-linux: the AppDir has no Mermaid MIME package" >&2
    exit 1
fi
for plugin in platforms/libqxcb.so platforms/libqoffscreen.so imageformats/libqsvg.so tls/libqopensslbackend.so; do
    if [ ! -f "$appdir/usr/plugins/$plugin" ]; then
        echo "package-linux: the AppDir has no $plugin" >&2
        exit 1
    fi
done
echo "package-linux: $out/$OUTPUT ($qt_libs Qt libraries, all bundled)"
