#!/bin/sh
# Runs on the reMarkable when the 4-finger hold gesture fires.
#
#   in any of our apps -> stop it (its wrapper restores xochitl)
#   in the launcher    -> ignore (the launcher has its own buttons)
#   in the tablet UI   -> open the launcher; the user picks an app or goes back
#
# CRITICAL: every app that owns the e-paper display must be listed in
# KNOWN_APPS. Only one process can hold /tmp/epframebuffer.lock; if this
# script opens the launcher while an app still holds it, the launcher aborts,
# xochitl then cannot start either, systemd crash-loops it and the device
# REBOOTS. That is exactly what an unlisted app caused.
set -u

APPS_DIR="/home/root/apps"
CHOICE_FILE="/tmp/launcher-choice"
TIMEOUT="${MODESWITCH_LAUNCHER_TIMEOUT:-120}"

# "<process name>:<systemd unit>" for every app that takes over the display.
# Add one entry per app you install, e.g. "rm_chat:rm-chat.service".
KNOWN_APPS="rm_chat:rm-chat.service"

# One switch at a time.
exec 9>/tmp/mode-toggle.lock
flock -n 9 || exit 0

if pidof rm_launcher >/dev/null 2>&1; then
    echo "[toggle] launcher already open, ignoring"
    exit 0
fi

# In an app? Then this gesture means "back to the tablet".
for entry in $KNOWN_APPS; do
    proc=$(echo "$entry" | cut -d: -f1)
    unit=$(echo "$entry" | cut -d: -f2)
    if pidof "$proc" >/dev/null 2>&1; then
        echo "[toggle] leaving $proc"
        systemctl stop "$unit"          # wrapper trap restarts xochitl
        exit 0
    fi
done

echo "[toggle] leaving tablet mode, opening launcher"
systemctl stop xochitl

# Wait for the display to actually be free. Checking xochitl alone is not
# enough - any leftover app holding the framebuffer would make the launcher
# abort and take the device down with it.
i=0
while [ "$i" -lt 30 ]; do
    busy=""
    pidof xochitl >/dev/null 2>&1 && busy="xochitl"
    for entry in $KNOWN_APPS; do
        proc=$(echo "$entry" | cut -d: -f1)
        pidof "$proc" >/dev/null 2>&1 && busy="$proc"
    done
    [ -z "$busy" ] && break
    sleep 0.5
    i=$((i + 1))
done

if [ -n "$busy" ]; then
    echo "[toggle] '$busy' still holds the display after 15s; aborting rather"
    echo "         than starting a second one and crash-looping the device" >&2
    systemctl start xochitl
    exit 1
fi
sleep 1

rm -f "$CHOICE_FILE"
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="rotate=180:invertx"
export QT_QUICK_BACKEND=epaper
"$APPS_DIR/rm_launcher" -platform epaper \
    --config "$APPS_DIR/apps.json" \
    --choice-file "$CHOICE_FILE" \
    --timeout "$TIMEOUT"

choice="$(cat "$CHOICE_FILE" 2>/dev/null || true)"
rm -f "$CHOICE_FILE"

case "$choice" in
    "" | tablet)
        echo "[toggle] back to tablet"
        systemctl start xochitl
        ;;
    *)
        echo "[toggle] starting: $choice"
        # A unit that crash-looped earlier may sit in the start-limit-hit
        # state; without this, choosing it does nothing and the launcher
        # appears broken.
        for entry in $KNOWN_APPS; do
            systemctl reset-failed "$(echo "$entry" | cut -d: -f2)" 2>/dev/null
        done
        if ! sh -c "$choice"; then
            echo "[toggle] '$choice' failed, restoring tablet"
            systemctl start xochitl
        fi
        ;;
esac
