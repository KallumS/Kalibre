#!/bin/sh
# Puts Kalibre in your applications menu (decision 0139): it stays in this
# folder; the menu, the icon and .kalibre songs point here. Nothing is
# written outside your home folder, and no password is asked for.
# Run it again after moving this folder. To take it out again: ./install.sh --remove
set -e
here="$(cd "$(dirname "$0")" && pwd)"
apps="$HOME/.local/share/applications"
icons="$HOME/.local/share/icons/hicolor/256x256/apps"
mime="$HOME/.local/share/mime/packages"
bin="$HOME/.local/bin"

if [ "$1" = "--remove" ]; then
    rm -f "$apps/kalibre.desktop" "$icons/kalibre.png" "$mime/kalibre.xml" "$bin/kalibre"
    command -v update-desktop-database >/dev/null && update-desktop-database "$apps" || true
    command -v update-mime-database >/dev/null && update-mime-database "$HOME/.local/share/mime" || true
    echo "Kalibre is out of the applications menu. This folder is untouched: delete it to remove Kalibre."
    exit 0
fi

mkdir -p "$apps" "$icons" "$mime" "$bin"
sed "s|KALIBRE_DIR|$here|g" "$here/kalibre.desktop" > "$apps/kalibre.desktop"
cp "$here/kalibre.png" "$icons/kalibre.png"
cp "$here/kalibre-mime.xml" "$mime/kalibre.xml"
ln -sf "$here/Kalibre" "$bin/kalibre"
command -v update-desktop-database >/dev/null && update-desktop-database "$apps" || true
command -v update-mime-database >/dev/null && update-mime-database "$HOME/.local/share/mime" || true
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
echo "Kalibre is in your applications menu, and opens .kalibre songs. In a terminal: kalibre"
