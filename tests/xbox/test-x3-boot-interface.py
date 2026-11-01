#!/usr/bin/env python3
"""Exercise the actual optional device through QEMU's I/O address space.

No BIOS, MCPX, games, or HDD downloads are needed. Run under xvfb on Linux.
Usage: python3 tests/xbox/test-x3-boot-interface.py /path/to/xemu
"""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def check(executable, enabled):
    with tempfile.TemporaryDirectory(prefix="x3-port-check-") as name:
        root = Path(name)
        (root / "empty-rom.bin").write_bytes(bytes([255]) * 65536)
        (root / "eeprom.bin").write_bytes(bytes(256))
        config = ("[general]\nshow_welcome = false\n[sys]\n"
                  "mem_limit = '64'\n[sys.files]\nbootrom_path = ''\n"
                  "hdd_path = ''\ndvd_path = ''\neeprom_path = "
                  + json.dumps(str(root / "eeprom.bin")) + "\n"
                  "flashrom_path = " + json.dumps(str(root / "empty-rom.bin"))
                  + "\n")
        (root / "xemu.toml").write_text(config)
        # TCP endpoints work on both Windows and Linux.
        listeners = [socket.socket() for _ in range(2)]
        for listener in listeners:
            listener.bind(("127.0.0.1", 0))
        ports = [listener.getsockname()[1] for listener in listeners]
        for listener in listeners:
            listener.close()
        args = [str(executable), "-config_path", str(root / "xemu.toml"),
                "-S", "-accel", "qtest", "-qtest", f"tcp:127.0.0.1:{ports[0]},server=on,wait=off",
                "-qmp", f"tcp:127.0.0.1:{ports[1]},server=on,wait=off"]
        if enabled:
            args += ["-device", "x3-boot-interface"]
        env = dict(os.environ, SDL_AUDIODRIVER="dummy")
        with (root / "host.log").open("w") as log:
            process = subprocess.Popen(args, stdout=log, stderr=log, env=env)
            try:
                deadline = time.monotonic() + 15
                while True:
                    if process.poll() is not None:
                        raise AssertionError((root / "host.log").read_text())
                    try:
                        io = socket.create_connection(("127.0.0.1", ports[0]),
                                                      timeout=1)
                        break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise TimeoutError("qtest startup")
                        time.sleep(.05)
                with io, socket.create_connection(("127.0.0.1", ports[1]),
                                                   timeout=5) as qmp:
                    io.settimeout(5)
                    stream = io.makefile("rwb", buffering=0)
                    control = qmp.makefile("rwb", buffering=0)
                    control.readline()
                    events = []

                    def require(condition, detail):
                        if not condition:
                            raise AssertionError(detail)

                    def command(value):
                        stream.write((value + "\n").encode())
                        result = stream.readline().decode().strip()
                        require(result.startswith("OK"), result)
                        return result.split()[1:]

                    def rpc(value):
                        control.write((json.dumps({"execute": value})
                                       + "\n").encode())
                        while True:
                            try:
                                line = control.readline()
                            except ConnectionResetError:
                                if value == "quit":
                                    return {}
                                raise
                            if not line and value == "quit":
                                return {}
                            result = json.loads(line)
                            if "event" in result:
                                events.append(result)
                            if "error" in result:
                                raise AssertionError(result)
                            if "return" in result:
                                return result

                    rpc("qmp_capabilities")
                    expected = 0xe1 if enabled else 0xff

                    def read(value, expected_value):
                        actual = int(command(value)[0], 0)
                        require(actual == expected_value,
                                f"{value}: {actual:#x} != {expected_value:#x}")

                    read("inb 0xf500", expected)
                    read("inb 0xf501", 0xff)
                    command("outb 0xf500 0")
                    read("inb 0xf500", expected)
                    read("inw 0xf500", 0xff00 | expected)
                    read("inl 0xf500", 0xffffff00 | expected)
                    events.clear()
                    rpc("system_reset")
                    # A command response only schedules reset. Retain events
                    # delivered before that response and wait for completion.
                    while not any(e["event"] == "RESET" for e in events):
                        result = json.loads(control.readline())
                        require("event" in result, result)
                        events.append(result)
                    read("inb 0xf500", expected)
                    rpc("quit")
                process.wait(timeout=10)
                require(process.returncode == 0, process.returncode)
            except Exception:
                log.flush()
                print((root / "host.log").read_text(), file=sys.stderr)
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)
    print("PASS: " + ("X3 ID, write protection, widths, adjacent port and reset"
                      if enabled else "default machine remains unchanged"))


if __name__ == "__main__":
    binary = Path(sys.argv[1]).resolve()
    check(binary, False)
    check(binary, True)
