#!/usr/bin/env bash
# Installs Markdown Notes for the current user on Linux (made for KDE Plasma):
# the app, its icon, an application-launcher entry with a "Quick capture"
# action, an optional desktop icon, and a global shortcut for quick capture.
# Nothing is installed system-wide and no sudo is needed.
#
#   ./install.sh                         build from source (release) and install
#   ./install.sh --appimage FILE         install a built AppImage instead
#   ./install.sh --capture-key Meta+Alt+N   choose the quick-capture shortcut
#   ./install.sh --capture-key none      don't set a quick-capture shortcut
#   ./install.sh --desktop-icon          also put a launcher on the desktop
#   ./install.sh --uninstall             remove everything this script installed
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ID=markdown-notes
DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
CONFIG_HOME=${XDG_CONFIG_HOME:-$HOME/.config}
APP_DIR=$HOME/.local/opt/$ID
BIN_LINK=$HOME/.local/bin/$ID
DESKTOP_FILE=$DATA_HOME/applications/$ID.desktop
ICON_DIR=$DATA_HOME/icons/hicolor
DESKTOP_DIR=$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")

appimage=""
binary=""
capture_key="Meta+Shift+N"
desktop_icon=0
uninstall=0

usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --appimage) appimage=$(realpath "$2"); shift 2 ;;
        --binary) binary=$(realpath "$2"); shift 2 ;;   # an already-built executable
        --capture-key) capture_key=$2; shift 2 ;;
        --desktop-icon) desktop_icon=1; shift ;;
        --uninstall) uninstall=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

say() { printf '  %s\n' "$*"; }

refresh_menu() {
    if command -v kbuildsycoca6 >/dev/null 2>&1; then
        kbuildsycoca6 >/dev/null 2>&1 || true
    elif command -v kbuildsycoca5 >/dev/null 2>&1; then
        kbuildsycoca5 >/dev/null 2>&1 || true
    fi
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$DATA_HOME/applications" || true
}

kconfig_write() {
    if command -v kwriteconfig6 >/dev/null 2>&1; then kwriteconfig6 "$@"
    elif command -v kwriteconfig5 >/dev/null 2>&1; then kwriteconfig5 "$@"
    else return 1
    fi
}

if [ "$uninstall" = 1 ]; then
    echo "Removing Markdown Notes…"
    rm -f "$DESKTOP_FILE" "$DESKTOP_DIR/$ID.desktop"
    [ -L "$BIN_LINK" ] && rm -f "$BIN_LINK"
    rm -rf "$APP_DIR"
    find "$ICON_DIR" -name "$ID.png" -delete 2>/dev/null || true
    rm -f "$ICON_DIR/scalable/apps/$ID.svg"
    kconfig_write --file kglobalshortcutsrc --group services --group "$ID.desktop" --key Capture --delete 2>/dev/null || true
    refresh_menu
    echo "Done. Your notes and settings were not touched."
    exit 0
fi

echo "Installing Markdown Notes…"

# 1. The app.
mkdir -p "$APP_DIR" "$(dirname "$BIN_LINK")"
if [ -n "$appimage" ]; then
    install -m755 "$appimage" "$APP_DIR/Markdown_Notes.AppImage"
    exec_path=$APP_DIR/Markdown_Notes.AppImage
    say "app: $exec_path (AppImage)"
else
    if [ -z "$binary" ]; then
        binary=$ROOT/build/release/src/markdown-notes
        echo "  building the release version (this can take a few minutes the first time)…"
        export PATH="$HOME/.local/bin:$PATH"
        (cd "$ROOT" && cmake --preset release >/dev/null && cmake --build --preset release --target markdown-notes >/dev/null)
    fi
    [ -x "$binary" ] || { echo "No executable at $binary" >&2; exit 1; }
    install -m755 "$binary" "$APP_DIR/$ID"
    exec_path=$APP_DIR/$ID
    say "app: $exec_path"
fi
ln -sf "$exec_path" "$BIN_LINK"
say "command: $BIN_LINK"

# 2. Icons.
install -Dm644 "$ROOT/packaging/linux/$ID.svg" "$ICON_DIR/scalable/apps/$ID.svg"
for size in 16 24 32 48 64 128 256 512; do
    install -Dm644 "$ROOT/packaging/icons/$ID-$size.png" "$ICON_DIR/${size}x${size}/apps/$ID.png"
done
say "icon: $ICON_DIR/scalable/apps/$ID.svg"

# 3. Application-launcher entry. The Exec path is quoted in case it contains spaces.
# The capture shortcut is only declared if the key isn't already bound elsewhere.
capture_line=""
taken=""
if [ "$capture_key" != "none" ] && [ -n "$capture_key" ]; then
    shortcuts=$CONFIG_HOME/kglobalshortcutsrc
    if [ -f "$shortcuts" ]; then
        taken=$(grep -v "^\[" "$shortcuts" | grep -F -- "$capture_key" | grep -v "^Capture=" | head -1 || true)
    fi
    [ -z "$taken" ] && capture_line="X-KDE-Shortcuts=$capture_key"
fi
mkdir -p "$(dirname "$DESKTOP_FILE")"
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=Markdown Notes
GenericName=Notes
Comment=Read and write your Markdown notes (Obsidian vault)
Exec="$exec_path" %f
Icon=$ID
Terminal=false
Categories=Office;TextEditor;Utility;
Keywords=notes;markdown;obsidian;vault;wiki;
MimeType=text/markdown;
StartupWMClass=$ID
Actions=Today;Capture;

[Desktop Action Today]
Name=Today's note
Icon=$ID
Exec="$exec_path" --today

[Desktop Action Capture]
Name=Quick capture
Icon=$ID
Exec="$exec_path" --capture
$capture_line
EOF
chmod 644 "$DESKTOP_FILE"
say "launcher: $DESKTOP_FILE"

# 4. Optional desktop icon (KDE needs it executable to trust it).
if [ "$desktop_icon" = 1 ]; then
    mkdir -p "$DESKTOP_DIR"
    install -m755 "$DESKTOP_FILE" "$DESKTOP_DIR/$ID.desktop"
    say "desktop icon: $DESKTOP_DIR/$ID.desktop"
fi

# 5. Global shortcut for quick capture.
if [ -n "$taken" ]; then
    say "shortcut: $capture_key is already in use ($taken); not set."
    say "          Pick another with --capture-key, or set one in System Settings > Shortcuts."
elif [ -n "$capture_line" ]; then
    if kconfig_write --file kglobalshortcutsrc --group services --group "$ID.desktop" --key Capture "$capture_key"; then
        say "shortcut: $capture_key opens quick capture (active after you next log in,"
        say "          or set it straight away in System Settings > Shortcuts > Markdown Notes)"
    else
        say "shortcut: not KDE; bind '$exec_path --capture' to a key in your desktop's settings."
    fi
fi

refresh_menu
echo "Done. Markdown Notes is in your application launcher."
case ":$PATH:" in
    *":$HOME/.local/bin:"*) ;;
    *) echo "Tip: add ~/.local/bin to PATH to run '$ID' from a terminal." ;;
esac
