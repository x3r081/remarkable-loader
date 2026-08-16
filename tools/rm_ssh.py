#!/usr/bin/env python3
"""Run a command on the reMarkable over SSH using password auth (pexpect)."""
import sys, pexpect

import os
HOST = "root@" + os.environ.get("RM_HOST", "10.11.99.1")
PASS = os.environ.get("RM_PASSWORD") or __import__("getpass").getpass("root password: ")

def main():
    cmd = " ".join(sys.argv[1:])
    child = pexpect.spawn(
        "ssh",
        ["-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=~/.ssh/known_hosts",
         "-o", "PubkeyAuthentication=no", HOST, cmd],
        timeout=120, encoding="utf-8", codec_errors="replace",
    )
    i = child.expect(["[Pp]assword:", pexpect.EOF, pexpect.TIMEOUT])
    if i == 0:
        child.sendline(PASS)
    child.expect(pexpect.EOF)
    sys.stdout.write(child.before or "")
    child.close()
    sys.exit(child.exitstatus or 0)

main()
