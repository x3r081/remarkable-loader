#!/usr/bin/env python3
"""Manage the on-tablet mode switcher (gesture daemon + app launcher).

    ./tools/switcher.py deploy      upload daemon, launcher, toggle script, apps.json
    ./tools/switcher.py enable      install + start the gesture daemon (boot persistent)
    ./tools/switcher.py disable     stop + remove the gesture daemon
    ./tools/switcher.py status      daemon / units / current mode
    ./tools/switcher.py logs        recent daemon + toggle output
    ./tools/switcher.py test        end-to-end gesture test via a virtual device
    ./tools/switcher.py screenshot  render the launcher offscreen, pull a PNG

The daemon watches the touch panel for a 4-finger hold (~1.2 s):
    tablet mode   -> opens the app launcher
    inside an app -> returns to the tablet
"""
import argparse
import os
import sys

import pexpect

HOST = os.environ.get("RM_HOST", "10.11.99.1")
USER = os.environ.get("RM_USER", "root")


def _password():
    """Device root password.

    Never hard-code it: set RM_PASSWORD, or be prompted. Find it on the tablet
    under Settings -> Help -> About -> Copyrights and licenses (bottom).
    """
    pw = os.environ.get("RM_PASSWORD")
    if not pw:
        import getpass
        pw = getpass.getpass(f"root password for {HOST}: ")
    return pw


PASSWORD = None  # resolved lazily by _pw()


def _pw():
    global PASSWORD
    if PASSWORD is None:
        PASSWORD = _password()
    return PASSWORD
APPS_DIR = "/home/root/apps"
UNIT = "/etc/systemd/system/modeswitch.service"

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
SSH_OPTS = ["-o", "StrictHostKeyChecking=no", "-o", "PubkeyAuthentication=no",
            "-o", "ConnectTimeout=10"]


def _spawn(prog, argv, timeout=180):
    child = pexpect.spawn(prog, SSH_OPTS + argv, timeout=timeout,
                          encoding="utf-8", codec_errors="replace")
    if child.expect(["[Pp]assword:", pexpect.EOF, pexpect.TIMEOUT]) == 0:
        child.sendline(_pw())
    child.expect(pexpect.EOF)
    out = (child.before or "").strip()
    child.close()
    return child.exitstatus, out


def ssh(cmd, timeout=180):
    return _spawn("ssh", [f"{USER}@{HOST}", cmd], timeout)


def push(local, remote):
    rc, out = _spawn("scp", [local, f"{USER}@{HOST}:{remote}"], 300)
    if rc not in (0, None):
        raise RuntimeError(f"upload of {local} failed: {out}")


def pull(remote, local):
    rc, out = _spawn("scp", [f"{USER}@{HOST}:{remote}", local], 300)
    if rc not in (0, None):
        raise RuntimeError(f"download of {remote} failed: {out}")


def cmd_deploy(args):
    files = [
        (os.path.join(REPO, "switcher", "build", "modeswitchd"), "modeswitchd", True),
        (os.path.join(REPO, "switcher", "build", "uinject"), "uinject", True),
        (os.path.join(REPO, "switcher", "build", "peninject"), "peninject", True),
        (os.path.join(REPO, "switcher", "mode-toggle.sh"), "mode-toggle.sh", True),
        (os.path.join(REPO, "launcher", "build", "rm_launcher"), "rm_launcher", True),
        (os.path.join(REPO, "switcher", "apps.json"), "apps.json", False),
    ]
    for local, _, _ in files:
        if not os.path.exists(local):
            print(f"Missing {local} - run tools/build.sh switcher and "
                  f"tools/build.sh launcher first", file=sys.stderr)
            return 1

    print(f"[*] uploading to {HOST}:{APPS_DIR}")
    ssh(f"mkdir -p {APPS_DIR}")
    # The daemon may be running the old binary; upload under a temp name and
    # move over to dodge ETXTBSY.
    for local, name, executable in files:
        push(local, f"{APPS_DIR}/.{name}.new")
        mode = "755" if executable else "644"
        ssh(f"mv {APPS_DIR}/.{name}.new {APPS_DIR}/{name} && "
            f"chmod {mode} {APPS_DIR}/{name}")
        print(f"  + {name}")

    _, active = ssh("systemctl is-active modeswitch.service 2>/dev/null || true")
    if "active" in active and "inactive" not in active:
        print("[*] restarting the gesture daemon on the new binary")
        ssh("systemctl restart modeswitch.service")
    return 0


def cmd_enable(args):
    rc, _ = ssh(f"test -x {APPS_DIR}/modeswitchd")
    if rc not in (0, None):
        print("Not deployed yet - run ./tools/switcher.py deploy first",
              file=sys.stderr)
        return 1
    print("[*] installing modeswitch.service")
    push(os.path.join(REPO, "switcher", "modeswitch.service"), UNIT)
    _, out = ssh("systemctl daemon-reload && "
                 "systemctl enable --now modeswitch.service && "
                 "systemctl is-active modeswitch.service")
    print(out)
    print("\nHold 4 fingers on the screen for ~1.2 s to switch modes.")
    return 0


def cmd_disable(args):
    print("[*] removing the gesture daemon")
    _, out = ssh("systemctl disable --now modeswitch.service 2>/dev/null; "
                 f"rm -f {UNIT}; systemctl daemon-reload; echo done")
    print(out)
    return 0


def cmd_status(args):
    _, out = ssh(
        "echo \"gesture daemon: $(systemctl is-active modeswitch.service 2>/dev/null)"
        " ($(systemctl is-enabled modeswitch.service 2>/dev/null || echo not-installed) at boot)\"; "
        "echo \"xochitl:        $(systemctl is-active xochitl)\"; "
        "if pidof rm_launcher >/dev/null; then echo 'mode:           launcher'; "
        "elif systemctl is-active xochitl >/dev/null; then echo 'mode:           tablet'; "
        "else echo 'mode:           (none?)'; fi; "
        f"echo \"apps.json:      $(ls -la {APPS_DIR}/apps.json 2>/dev/null | awk '{{print $5\" bytes\"}}')\"")
    print(out)
    return 0


def cmd_logs(args):
    _, out = ssh(f"journalctl -u modeswitch.service -n {args.lines} --no-pager "
                 "2>/dev/null || echo '(no journal)'")
    print(out)
    return 0


def cmd_test(args):
    """Full pipeline test with a virtual touch device - no physical touch needed."""
    print("[*] 1/3 arming a test daemon against a virtual touch device")
    # Marker file proves the toggle exec fired. Test daemon uses --device
    # pointing at the virtual node, so the REAL daemon (on pt_mt) is untouched.
    ssh("rm -f /tmp/modeswitch-test-marker")

    script = (
        f"cd {APPS_DIR} && "
        # start injector first so its device node exists (it holds 4 fingers
        # for 2 s, well past the 1.2 s threshold)...
        "( ./uinject 4 2000 >/tmp/uinject.log 2>&1 & ) && sleep 0.3 && "
        # ...find the newest event node (the virtual device)
        "DEV=$(ls -t /dev/input/event* | head -n 1) && echo \"test device: $DEV\" && "
        # BusyBox has no `timeout`; background the daemon and kill it after 6 s
        f"./modeswitchd --device $DEV --exec 'touch /tmp/modeswitch-test-marker' "
        ">/tmp/modeswitchd-test.log 2>&1 & "
        "TESTPID=$!; sleep 6; kill $TESTPID 2>/dev/null; "
        "head -n 5 /tmp/modeswitchd-test.log; "
        "test -f /tmp/modeswitch-test-marker && echo TEST-PASS || echo TEST-FAIL"
    )
    _, out = ssh(script, timeout=60)
    print(out)
    if "TEST-PASS" not in out:
        print("\nGesture pipeline test FAILED", file=sys.stderr)
        return 1
    print("\n[*] 2/3 daemon detected the synthetic 4-finger hold and ran its exec")

    _, active = ssh("systemctl is-active modeswitch.service 2>/dev/null || echo inactive")
    print(f"[*] 3/3 real daemon: {active.splitlines()[-1]}")
    return 0


def cmd_screenshot(args):
    dest = os.path.abspath(args.output)
    print("[*] rendering the launcher offscreen on the device")
    _, log = ssh(f"cd {APPS_DIR} && QT_QUICK_BACKEND=software ./rm_launcher "
                 f"-platform offscreen --config {APPS_DIR}/apps.json "
                 f"--screenshot {APPS_DIR}/launcher-shot.png 2>&1 | tail -n 3; "
                 f"ls -l {APPS_DIR}/launcher-shot.png")
    print(log)
    pull(f"{APPS_DIR}/launcher-shot.png", dest)
    print(f"[*] saved {dest}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("deploy").set_defaults(func=cmd_deploy)
    sub.add_parser("enable").set_defaults(func=cmd_enable)
    sub.add_parser("disable").set_defaults(func=cmd_disable)
    sub.add_parser("status").set_defaults(func=cmd_status)
    sub.add_parser("test").set_defaults(func=cmd_test)

    p_logs = sub.add_parser("logs")
    p_logs.add_argument("-n", "--lines", type=int, default=30)
    p_logs.set_defaults(func=cmd_logs)

    p_shot = sub.add_parser("screenshot")
    p_shot.add_argument("output", nargs="?",
                        default=os.path.join(REPO, "launcher-preview.png"))
    p_shot.set_defaults(func=cmd_screenshot)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
