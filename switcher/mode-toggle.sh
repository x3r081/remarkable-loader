#!/bin/sh
# Runs on the reMarkable when the trigger gesture fires.
#
#   in the stock UI  -> open the launcher; the user picks an app or goes back
#   in an app        -> stop it (its wrapper restores xochitl)
#   in the launcher  -> ignore (the launcher has its own buttons)
#
# Which mode we are in is decided by WHO OWNS THE DISPLAY, i.e. whether
# xochitl is running - not by whether an app process exists somewhere. A
# stray background process (a leftover test run, a crashed app that never
# reaped) would otherwise make every gesture mean "leave the app", silently
# disabling the launcher with no visible error.
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

# ---------------------------------------------------------------- app mode
if ! systemctl is-active --quiet xochitl; then
    stopped=""
    for entry in $KNOWN_APPS; do
        proc=$(echo "$entry" | cut -d: -f1)
        unit=$(echo "$entry" | cut -d: -f2)
        if pidof "$proc" >/dev/null 2>&1; then
            echo "[toggle] leaving $proc"
            systemctl stop "$unit"          # wrapper trap restores xochitl
            stopped="$proc"
            # An app started by hand is not under the unit; make sure it goes.
            i=0
            while [ "$i" -lt 10 ]; do
                pidof "$proc" >/dev/null 2>&1 || break
                sleep 0.5
                i=$((i + 1))
            done
            pidof "$proc" >/dev/null 2>&1 && killall "$proc" 2>/dev/null
        fi
    done

    # Nothing of ours is running, yet xochitl is not either: the tablet has no
    # UI at all. Restore it rather than leaving a dead screen.
    if [ -z "$stopped" ]; then
        echo "[toggle] no UI running, restoring the tablet"
    fi
    systemctl --no-block start xochitl
    exit 0
fi

# ------------------------------------------------------------- tablet mode
echo "[toggle] leaving tablet mode, opening launcher"

# Defensive: a stray app process from a crashed or hand-started run would
# fight the launcher for the display lock.
for entry in $KNOWN_APPS; do
    proc=$(echo "$entry" | cut -d: -f1)
    if pidof "$proc" >/dev/null 2>&1; then
        echo "[toggle] clearing stray $proc before opening the launcher"
        killall "$proc" 2>/dev/null
    fi
done

systemctl stop xochitl

# Wait for the display to actually be free. Only one process may hold
# /tmp/epframebuffer.lock; starting the launcher too early makes it abort,
# after which xochitl cannot start either and systemd crash-loops it.
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
    echo "[toggle] '$busy' still holds the display after 15s; aborting rather" >&2
    echo "         than starting a second one and crash-looping the device" >&2
    systemctl --no-block start xochitl
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
        systemctl --no-block start xochitl
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
            systemctl --no-block start xochitl
        fi
        ;;
esac
