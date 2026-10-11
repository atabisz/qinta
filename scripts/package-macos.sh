#!/usr/bin/env bash
# Package a built qinta as Qinta.app, with the Qt frameworks and plugins it loads copied in by
# macdeployqt, and zip it for download.
#
#   scripts/package-macos.sh <build directory> <output directory>
#
# The CMake target stays a plain executable (README's macOS section), so the bundle is assembled
# here rather than by MACOSX_BUNDLE, which would move the binary every test and command points at.
# Writes <output directory>/qinta-<version>-macos-<arch>.zip and leaves Qinta.app beside it.
#
# The bundle is signed ad hoc, which arm64 requires for anything to run once macdeployqt has
# rewritten the binaries. It is NOT notarized, so Gatekeeper blocks it on first open from a
# download until the user allows it (right-click, Open).
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
    echo "package-macos: no qinta executable in $build" >&2
    exit 2
fi

# A tagged commit is named by its tag; anything else by its commit, so a snapshot can never be
# mistaken for a release. (CI checks out without tags, so describe would differ from a local run.)
# QINTA_VERSION, when set, names a release build (build.yml sets it from the release input).
version=${QINTA_VERSION:-$(git -C "$root" describe --tags --exact-match 2>/dev/null || git -C "$root" rev-parse --short HEAD)}
short_version=$(sed -n 's/^project([a-z]* VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
arch=$(uname -m)
app="$out/Qinta.app"
rm -rf "$app" "$out"/qinta-*-macos-*.zip "$out/qinta.iconset"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$build/qinta" "$app/Contents/MacOS/qinta"

# The icon, from the same resources/qinta.ico the Windows build embeds: sips reads ICO, and
# iconutil wants every size of a standard iconset.
iconset="$out/qinta.iconset"
mkdir -p "$iconset"
sips -s format png "$root/resources/qinta.ico" --out "$out/qinta-source.png" >/dev/null
for size in 16 32 128 256 512; do
    sips -z $size $size "$out/qinta-source.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z $double $double "$out/qinta-source.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/qinta.icns"

cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>qinta</string>
    <key>CFBundleIdentifier</key><string>io.github.atabisz.qinta</string>
    <key>CFBundleName</key><string>Qinta</string>
    <key>CFBundleDisplayName</key><string>Qinta</string>
    <key>CFBundleIconFile</key><string>qinta</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$short_version</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key><string>Markdown document</string>
            <key>CFBundleTypeRole</key><string>Editor</string>
            <key>LSItemContentTypes</key>
            <array><string>net.daringfireball.markdown</string></array>
            <key>CFBundleTypeExtensions</key>
            <array><string>md</string><string>markdown</string><string>mmd</string></array>
        </dict>
    </array>
</dict>
</plist>
EOF
plutil -lint "$app/Contents/Info.plist" >/dev/null

qtbase=$(brew --prefix qtbase)
"$qtbase/bin/macdeployqt" "$app" -always-overwrite

# Plugins macdeployqt leaves out, copied in and then fixed up by a second macdeployqt pass. It
# deploys only the cocoa platform plugin, and offscreen is what runs qinta headless (the
# --printpages switches, and CI's check of this bundle). And Homebrew installs qtsvg as its own
# keg while macdeployqt looks only under qtbase's, so the SVG plugins come from the shared tree.
copied=0
for plugin in platforms/libqoffscreen.dylib imageformats/libqsvg.dylib iconengines/libqsvgicon.dylib; do
    [ -f "$app/Contents/PlugIns/$plugin" ] && continue
    for tree in "$qtbase/share/qt/plugins" "$(brew --prefix)/share/qt/plugins"; do
        if [ -f "$tree/$plugin" ]; then
            mkdir -p "$app/Contents/PlugIns/$(dirname "$plugin")"
            cp "$tree/$plugin" "$app/Contents/PlugIns/$plugin"
            copied=$((copied + 1))
            break
        fi
    done
done
if [ "$copied" -gt 0 ]; then
    "$qtbase/bin/macdeployqt" "$app" -always-overwrite >/dev/null
fi
for plugin in platforms/libqcocoa.dylib platforms/libqoffscreen.dylib imageformats/libqsvg.dylib; do
    if [ ! -f "$app/Contents/PlugIns/$plugin" ]; then
        echo "package-macos: the bundle has no $plugin" >&2
        exit 1
    fi
done
# Any TLS backend, not a named one: which Qt ships (SecureTransport, OpenSSL) depends on how
# Homebrew configured it. Without one, remote https images fail with no error a reader sees.
if ! ls "$app/Contents/PlugIns/tls/"*.dylib >/dev/null 2>&1; then
    echo "package-macos: the bundle has no TLS backend plugin" >&2
    exit 1
fi

# Self-contained means no Mach-O in the bundle still loads from Homebrew. The number of Mach-O
# files read is checked too, so a scan that found nothing cannot read as clean.
#
# For a library, otool -L prints its OWN install name before its dependencies, and that name is
# never loaded from; the first run of this check counted it and failed six frameworks for naming
# themselves. So the file's own id (otool -D) is dropped first.
homebrew_deps() {
    local self_id
    self_id=$(otool -D "$1" | tail -n +2)
    otool -L "$1" | tail -n +2 | awk '{print $1}' | grep -vxF "${self_id:-//none//}" |
        grep -E '^(/opt/homebrew|/usr/local)/' || true
}
# The control: Homebrew's own QtGui certainly loads from Homebrew, so a check that calls it clean
# has stopped seeing dependencies, and the clean bundle below would mean nothing.
if [ -z "$(homebrew_deps "$qtbase/lib/QtGui.framework/QtGui")" ]; then
    echo "package-macos: the dependency check found nothing in Homebrew's own QtGui; it is broken" >&2
    exit 1
fi
machos=0
leaks=0
while IFS= read -r f; do
    if file "$f" | grep -q 'Mach-O'; then
        machos=$((machos + 1))
        found=$(homebrew_deps "$f")
        if [ -n "$found" ]; then
            echo "package-macos: $f still loads from Homebrew:" >&2
            echo "$found" >&2
            leaks=$((leaks + 1))
        fi
    fi
done < <(find "$app" -type f)
if [ "$machos" -lt 10 ] || [ "$leaks" -ne 0 ]; then
    echo "package-macos: $machos Mach-O files read, $leaks loading from outside the bundle" >&2
    exit 1
fi

codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

zip="$out/qinta-$version-macos-$arch.zip"
ditto -c -k --sequesterRsrc --keepParent "$app" "$zip"
echo "package-macos: $zip ($machos Mach-O files, none loading from Homebrew)"
