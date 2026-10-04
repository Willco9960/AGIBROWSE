"""Read only categorical metadata in the narrowly marked Windows CI fixture."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
import time

MARKER = "AGI-BROWSE isolated profile fixture v1"
USER = re.compile(r"p-[0-9a-f]{32}\Z")


class Pins:
    """Keep verified directory/file identities stable throughout each snapshot."""
    def __init__(self):
        self.handles = []
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD,
            wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
            wintypes.HANDLE]
        self.api.CreateFileW.restype = wintypes.HANDLE
        self.api.GetFileInformationByHandle.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]

    def pin(self, path, directory=False):
        handle = self.api.CreateFileW(str(path), 0x80000000, 1, None, 3,
                                      0x02200000, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ValueError("unavailable")
        self.handles.append(handle)
        info = (wintypes.DWORD * 13)()
        if not self.api.GetFileInformationByHandle(handle, ctypes.byref(info)):
            raise ValueError("unavailable")
        if info[0] & 0x400 or bool(info[0] & 0x10) != directory:
            raise ValueError("untrusted")

    def close(self):
        for handle in reversed(self.handles):
            self.api.CloseHandle(handle)


def bounded_counts(connection):
    # Fixed projections retrieve no cookie name, value, host, path or key.
    predicates = ("1", "is_persistent=1", "has_expires=1",
                  "length(encrypted_value)>0")
    return [connection.execute(
        "SELECT count(*) FROM (SELECT 1 FROM cookies WHERE " + predicate +
        " LIMIT 2)").fetchone()[0] for predicate in predicates]


def profile_snapshot(path, pins):
    pins.pin(path, True)
    network = path / "Network"
    result = {"databaseExists": False, "walExists": False, "status": 0,
              "rows": None, "persistentRows": None, "expiryRows": None,
              "encryptedRows": None}
    if not network.exists():
        return result
    pins.pin(network, True)
    database = network / "Cookies"
    if not database.exists():
        return result
    pins.pin(database)
    result["databaseExists"] = True
    wal = network / "Cookies-wal"
    if wal.exists():
        pins.pin(wal)
        result["walExists"] = True
        # Immutable SQLite ignores WAL. Report unavailable instead of silently
        # querying a stale main file or creating a shared-memory sidecar.
        result["status"] = 2
        return result
    if database.stat().st_size > 64 * 1024 * 1024:
        result["status"] = 2
        return result
    try:
        connection = sqlite3.connect(database.as_uri() + "?mode=ro&immutable=1",
                                     uri=True, timeout=1)
        try:
            connection.execute("PRAGMA query_only=ON")
            deadline = time.monotonic() + 1
            connection.set_progress_handler(lambda: time.monotonic() > deadline, 1000)
            counts = bounded_counts(connection)
        finally:
            connection.close()
        for key, count in zip(("rows", "persistentRows", "expiryRows", "encryptedRows"), counts):
            result[key] = count  # 0 absent,1 one,2 at least two.
        result["status"] = 1
    except (sqlite3.Error, OSError, ValueError):
        result["status"] = 2
    return result


def snapshot(root):
    if os.name != "nt":
        return {"status": 2}
    pins = None
    try:
        root = Path(root).absolute()
        if (len(root.drive) != 2 or root.drive[1] != ":" or root.name != "profiles"
                or not re.fullmatch(r"[0-9a-f]{32}", root.parent.name)):
            return {"status": 3}
        pins = Pins()
        for ancestor in reversed((root, *root.parents)):
            pins.pin(ancestor, True)
        marker = root.parent / "profile-fixture.marker"
        pins.pin(marker)
        if marker.stat().st_size > 128 or marker.read_text(encoding="utf-8-sig").strip() != MARKER:
            return {"status": 3}
        users = []
        with os.scandir(root) as entries:
            for count, entry in enumerate(entries):
                if count >= 128:
                    return {"status": 3}
                if USER.fullmatch(entry.name):
                    path = root / entry.name
                    pins.pin(path, True)
                    identity = path / "profile.identity"
                    pins.pin(identity)
                    if (identity.stat().st_size > 128 or identity.read_bytes() !=
                            ("AGI-BROWSE profile v1\n" + entry.name + "\n").encode()
                            or (path / "deletion.pending").exists()):
                        return {"status": 3}
                    users.append(path)
        if len(users) != 1:
            return {"status": 3}
        local_state = root / "Local State"
        local_exists = local_state.exists()
        if local_exists:
            pins.pin(local_state)
        return {"status": 0, "localStateExists": local_exists,
                "profiles": [profile_snapshot(path, pins) for path in
                             (root / "Default", root / "Agent", users[0])]}
    except Exception:
        return {"status": 2}
    finally:
        if pins is not None:
            pins.close()


if __name__ == "__main__":
    try:
        result = snapshot(sys.argv[1]) if len(sys.argv) == 2 else {"status": 3}
        print(json.dumps(result, separators=(",", ":")))
    except Exception:
        print('{"status":2}')
