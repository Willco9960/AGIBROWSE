"""Non-GUI checks against only a fresh marked fixture under this repo's build."""
import hashlib
import json
from pathlib import Path
import runpy
import sqlite3
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HELPER = runpy.run_path(str(ROOT / "tools/cef/profile-storage-diagnostic.py"))
checks = 0


def check(condition):
    global checks
    if not condition:
        raise AssertionError("diagnostic contract")
    checks += 1


def run():
    build = (ROOT / "build").resolve()
    temporary = tempfile.TemporaryDirectory(prefix="task013-diagnostic-", dir=build)
    owned = Path(temporary.name).resolve()
    try:
        check(owned.is_relative_to(build) and owned != build)
        suite = owned / ("a" * 32)
        profile = suite / "profiles"
        profile.mkdir(parents=True)
        marker = suite / "profile-fixture.marker"
        marker.write_text(HELPER["MARKER"] + "\n")
        user = "p-" + "b" * 32
        for name in ("Default", "Agent", user):
            (profile / name).mkdir()
        identity = profile / user / "profile.identity"
        identity.write_bytes(("AGI-BROWSE profile v1\n" + user + "\n").encode())
        observation = HELPER["snapshot"](profile)
        check(observation["status"] == 0)
        check(observation["localStateExists"] is False)
        check(all(item["databaseExists"] is False for item in observation["profiles"]))
        network = profile / "Default" / "Network"
        network.mkdir()
        database = network / "Cookies"
        connection = sqlite3.connect(database)
        connection.execute("CREATE TABLE cookies(is_persistent INTEGER,has_expires INTEGER,encrypted_value BLOB,value TEXT)")
        connection.executemany("INSERT INTO cookies VALUES(?,?,?,?)",
                              [(1, 1, b"SECRET_COOKIE", "SECRET_PAGE")] * 3)
        connection.commit()
        connection.close()
        before = hashlib.sha256(database.read_bytes()).digest()
        observation = HELPER["snapshot"](profile)
        first = observation["profiles"][0]
        check(first["status"] == 1 and first["databaseExists"] is True)
        check([first[name] for name in ("rows", "persistentRows", "expiryRows", "encryptedRows")] == [2] * 4)
        check(hashlib.sha256(database.read_bytes()).digest() == before)
        exported = json.dumps(observation)
        check(all(secret not in exported for secret in ("SECRET_COOKIE", "SECRET_PAGE", user, str(profile))))
        check(not (network / "Cookies-shm").exists())
        wal = network / "Cookies-wal"
        wal.write_bytes(b"unread WAL")
        first = HELPER["snapshot"](profile)["profiles"][0]
        check(first["status"] == 2 and first["walExists"] and first["rows"] is None)
        wal.unlink()
        marker.write_text("untrusted")
        check(HELPER["snapshot"](profile) == {"status": 3})
        marker.write_text(HELPER["MARKER"])
        identity.write_bytes(b"wrong identity")
        check(HELPER["snapshot"](profile) == {"status": 3})
        check(HELPER["snapshot"](suite) == {"status": 3})
        connection = sqlite3.connect(":memory:")
        connection.execute("CREATE TABLE cookies(is_persistent INTEGER,has_expires INTEGER,encrypted_value BLOB)")
        check(HELPER["bounded_counts"](connection) == [0] * 4)
        connection.execute("INSERT INTO cookies VALUES(1,1,X'01')")
        check(HELPER["bounded_counts"](connection) == [1] * 4)
        connection.close()
    finally:
        # Resolve and verify the exact temporary target before recursive cleanup.
        if owned.resolve().is_relative_to(build) and owned.resolve() != build:
            temporary.cleanup()


if __name__ == "__main__":
    try:
        run()
        print(f"PASS profile storage diagnostic checks={checks}")
    except Exception:
        print("FAIL profile storage diagnostic contract")
        raise SystemExit(1)
