#!/usr/bin/env bash
# Builds build/appimage/Markdown_Notes-<arch>.AppImage with linuxdeploy and its Qt
# plugin. Includes the Wayland platform plugin (with the shell-integration
# plugins it needs) and xcb as a fallback.
# Requires: qmake6 (or QMAKE) from the Qt used for the build, curl, and libfuse2.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/appimage"
TOOLS="$OUT/tools"
ARCH=$(uname -m)
mkdir -p "$OUT" "$TOOLS"

cmake --preset release -S "$ROOT"
cmake --build "$ROOT/build/release" --target markdown-notes

rm -rf "$OUT/AppDir"
DESTDIR="$OUT/AppDir" cmake --install "$ROOT/build/release" --prefix /usr
install -Dm644 "$ROOT/packaging/linux/markdown-notes.desktop" "$OUT/AppDir/usr/share/applications/markdown-notes.desktop"
install -Dm644 "$ROOT/packaging/linux/markdown-notes.svg" "$OUT/AppDir/usr/share/icons/hicolor/scalable/apps/markdown-notes.svg"
DOC="$OUT/AppDir/usr/share/doc/markdown-notes"
install -Dm644 "$ROOT/LICENSE" "$DOC/LICENSE"
install -Dm644 "$ROOT/THIRD_PARTY_NOTICES.md" "$DOC/THIRD_PARTY_NOTICES.md"
install -Dm644 "$ROOT/third_party/md4c/LICENSE.md" "$DOC/md4c-LICENSE.md"
install -Dm644 "$ROOT/third_party/microtex/LICENSE" "$DOC/microtex-LICENSE"
cp -r "$ROOT/third_party/microtex/res/fonts/licences" "$DOC/microtex-font-licences"

fetch() {
    local url=$1 dest=$2
    [ -x "$dest" ] || { curl -fsSL -o "$dest" "$url" && chmod +x "$dest"; }
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage" "$TOOLS/linuxdeploy"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage" "$TOOLS/linuxdeploy-plugin-qt"

export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so;libqxcb.so"
export EXTRA_QT_MODULES="waylandclient"
export PATH="$TOOLS:$PATH"
# Qt's LGPL notices go with the package.
QT_PREFIX=$("$QMAKE" -query QT_INSTALL_PREFIX)
for f in "$QT_PREFIX"/LICENSES/* "$QT_PREFIX"/../../Licenses/*; do
    [ -f "$f" ] && install -Dm644 "$f" "$DOC/qt-licences/$(basename "$f")"
done

cd "$OUT"
"$TOOLS/linuxdeploy" --appdir AppDir --plugin qt --output appimage \
    -d AppDir/usr/share/applications/markdown-notes.desktop \
    -i AppDir/usr/share/icons/hicolor/scalable/apps/markdown-notes.svg
ls -la "$OUT"/*.AppImage
