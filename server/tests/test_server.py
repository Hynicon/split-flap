"""End-to-end tests against a real server process.

    pip install -r server/requirements.txt pytest httpx
    pytest server/tests
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import httpx
import pytest

SERVER_DIR = Path(__file__).resolve().parent.parent
PASSWORD = "correct horse battery"
TOKEN = "device-token-0123456789abcdef"
HOLD = 2.0
DEVICE = {"Authorization": f"Bearer {TOKEN}"}
CONFIG = {"mode": "cycle", "text": "", "words": ["TACOS"], "platforms": {"tiktok": {"enabled": True, "source": "api", "manual_count": 0}}}
STATUS = {"display": {"frame": "  HELLO  "}, "uptime_s": 12}


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Server:
    def __init__(self, data: Path, **env: str) -> None:
        self.data = data
        self.env = env
        self.port = free_port()
        self.url = f"http://127.0.0.1:{self.port}"
        self.proc: subprocess.Popen | None = None

    def start(self) -> None:
        env = dict(os.environ, APP_PASSWORD=PASSWORD, DEVICE_TOKEN=TOKEN, DATA_DIR=str(self.data), HOLD_SECONDS=str(HOLD), **self.env)
        self.proc = subprocess.Popen([sys.executable, "-m", "uvicorn", "app:app", "--port", str(self.port), "--log-level", "warning", "--no-proxy-headers"],
                                     cwd=SERVER_DIR, env=env)
        for _ in range(100):
            try:
                httpx.get(self.url + "/healthz", timeout=1)
                return
            except httpx.HTTPError:
                time.sleep(0.1)
        raise RuntimeError("server did not start")

    def stop(self) -> None:
        if self.proc:
            self.proc.terminate()
            self.proc.wait(10)
            self.proc = None


@pytest.fixture
def server(tmp_path):
    s = Server(tmp_path)
    s.start()
    yield s
    s.stop()


def signed_in(server: Server) -> httpx.Client:
    client = httpx.Client(base_url=server.url, timeout=10)
    r = client.post("/login", data={"password": PASSWORD})
    assert r.status_code == 303 and r.headers["location"] == "/"
    return client


def sync(server: Server, timeout: float = 10, **body) -> dict:
    r = httpx.post(server.url + "/api/device/sync", json=body, headers=DEVICE, timeout=timeout)
    assert r.status_code == 200, r.text
    return r.json()


def report(server: Server) -> None:
    """The display's first sync: it reports its settings and status, then is left holding."""
    threading.Thread(target=sync, args=(server,), kwargs={"config": CONFIG, "status": STATUS}, daemon=True).start()
    time.sleep(0.3)


# ---- login in front of everything ----

def test_everything_needs_a_login(server):
    anon = httpx.Client(base_url=server.url)
    for path in ["/", "/index.html", "/app.js", "/manifest.webmanifest"]:
        r = anon.get(path)
        assert r.status_code == 303 and r.headers["location"] == "/login", path
    for path in ["/api/status", "/api/config"]:
        assert anon.get(path).status_code == 401, path
    for path in ["/api/config", "/api/show", "/api/rehome", "/api/tokens"]:
        assert anon.post(path, json={"text": "HI"}).status_code == 401, path
    assert anon.get("/login").status_code == 200
    assert anon.get("/style.css").status_code == 200  # the sign-in page's stylesheet


def test_wrong_password_and_lockout(server):
    anon = httpx.Client(base_url=server.url, timeout=10)
    assert anon.post("/login", data={"password": "nope"}).status_code == 401
    assert "session" not in anon.cookies
    for _ in range(4):
        anon.post("/login", data={"password": "nope"})
    # five wrong tries: locked out, even with the right password
    assert anon.post("/login", data={"password": PASSWORD}).status_code == 429


def test_login_logout(server):
    client = signed_in(server)
    assert client.get("/").status_code == 200
    assert "httponly" in client.post("/login", data={"password": PASSWORD}).headers["set-cookie"].lower()
    assert client.post("/logout").status_code == 303
    assert client.get("/api/status").status_code == 401


def test_forged_and_expired_sessions_are_refused(server):
    for cookie in ["", "garbage", "9999999999.deadbeef", f"{int(time.time()) - 10}.{'0' * 64}"]:
        r = httpx.get(server.url + "/api/status", cookies={"session": cookie})
        assert r.status_code == 401, cookie


def test_password_header_for_scripts(server):
    assert httpx.get(server.url + "/api/status", headers={"X-Admin-Key": PASSWORD}).status_code == 200
    assert httpx.get(server.url + "/api/status", headers={"X-Admin-Key": "wrong"}).status_code == 401


def test_password_header_guesses_are_locked_out_too(server):
    for i in range(5):
        assert httpx.get(server.url + "/api/status", headers={"X-Admin-Key": f"guess-{i}"}).status_code == 401
    # locked: the right password no longer works from here, by header or by form
    assert httpx.get(server.url + "/api/status", headers={"X-Admin-Key": PASSWORD}).status_code == 401
    assert httpx.post(server.url + "/login", data={"password": PASSWORD}).status_code == 429


def test_made_up_forwarding_header_does_not_dodge_the_lockout(server):
    for i in range(5):
        httpx.post(server.url + "/login", data={"password": "nope"}, headers={"X-Forwarded-For": f"10.0.0.{i}"}, timeout=10)
    r = httpx.post(server.url + "/login", data={"password": PASSWORD}, headers={"X-Forwarded-For": "10.9.9.9"})
    assert r.status_code == 429


def test_guesses_from_many_addresses_do_not_lock_out_the_owner(tmp_path):
    s = Server(tmp_path, TRUST_PROXY="1")
    s.start()
    try:
        def guess(i: int) -> None:
            for _ in range(5):
                httpx.post(s.url + "/login", data={"password": "nope"}, headers={"X-Forwarded-For": f"10.0.{i}.1"}, timeout=20)
        threads = [threading.Thread(target=guess, args=(i,)) for i in range(10)]  # 50 wrong passwords
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        def login(ip: str, password: str = PASSWORD) -> int:
            return httpx.post(s.url + "/login", data={"password": password}, headers={"X-Forwarded-For": ip}, timeout=20).status_code
        # each guessing network is locked out, also from its other addresses
        assert login("10.0.3.1") == 429
        assert login("10.0.3.77") == 429
        # under attack, a new network gets two wrong guesses, then is locked out too
        assert login("10.7.7.7", "nope") == 401
        assert login("10.7.7.7", "nope") == 401
        assert login("10.7.7.7") == 429
        # someone on a network that hasn't guessed can still sign in
        assert login("10.9.9.9") == 303
        # and a network that signed in recently isn't caught by the attack rule
        assert login("10.9.9.10", "typo") == 401
        assert login("10.9.9.10", "typo") == 401
        assert login("10.9.9.10") == 303
    finally:
        s.stop()


def test_full_failure_table_refuses_new_networks_instead_of_forgetting(tmp_path):
    s = Server(tmp_path, TRUST_PROXY="1", MAX_NETWORKS="3")
    s.start()
    try:
        def login(ip: str, password: str = PASSWORD) -> int:
            return httpx.post(s.url + "/login", data={"password": password}, headers={"X-Forwarded-For": ip}, timeout=20).status_code
        assert login("10.50.0.1") == 303  # this network is now trusted
        for i in range(3):
            assert login(f"10.0.{i}.1", "nope") == 401  # fills the table
        assert login("10.0.9.1") == 429  # new network, table full: refused
        for _ in range(4):
            login("10.0.0.1", "nope")
        assert login("10.0.0.1") == 429  # its lockout was kept
        assert login("10.50.0.1") == 303  # a trusted network still gets in
    finally:
        s.stop()


def test_signed_in_session_survives_a_lockout(server):
    client = signed_in(server)
    for i in range(6):
        httpx.get(server.url + "/api/status", headers={"X-Admin-Key": f"guess-{i}"})
    assert client.get("/api/status").status_code == 200


def test_files_outside_the_app_are_not_served(server):
    client = signed_in(server)
    for path in ["/../app.py", "/..%2f..%2fserver%2fapp.py", "/nope.html", "/%2e%2e/%2e%2e/platformio.ini"]:
        assert client.get(path).status_code == 404, path


# ---- the display's side ----

def test_device_token_required(server):
    assert httpx.post(server.url + "/api/device/sync", json={}).status_code == 401
    assert httpx.post(server.url + "/api/device/sync", json={}, headers={"Authorization": "Bearer wrong-token-000000"}).status_code == 401
    # a signed-in person is not the display
    assert signed_in(server).post("/api/device/sync", json={}).status_code == 401


def test_before_the_display_connects(server):
    client = signed_in(server)
    assert client.get("/api/config").status_code == 503
    s = client.get("/api/status").json()
    assert s["via"] == "server" and s["device"] == {"online": False, "last_seen": 0, "pending": 0, "errors": [], "known": False}


def test_status_and_config_mirror(server):
    client = signed_in(server)
    report(server)
    s = client.get("/api/status").json()
    assert s["display"]["frame"] == "  HELLO  " and s["device"]["online"] and s["device"]["known"]
    assert client.get("/api/config").json()["words"] == ["TACOS"]


def test_empty_sync_is_held_then_returns_nothing(server):
    start = time.time()
    assert sync(server) == {"commands": []}
    assert HOLD - 0.3 <= time.time() - start < HOLD + 2


def test_change_reaches_a_waiting_display_at_once(server):
    client = signed_in(server)
    sync_result: dict = {}
    t = threading.Thread(target=lambda: sync_result.update(sync(server, config=CONFIG, status=STATUS)))
    t.start()
    time.sleep(0.4)
    start = time.time()
    r = client.post("/api/config", json={"words": ["TACOS", "NACHOS"]})
    assert r.json() == {"ok": True, "queued": False}
    t.join(5)
    assert time.time() - start < 1.0  # not the full hold
    (cmd,) = sync_result["commands"]
    assert cmd["type"] == "config" and cmd["body"] == {"words": ["TACOS", "NACHOS"]}
    # The app sees the edit before the display confirms it.
    assert client.get("/api/config").json()["words"] == ["TACOS", "NACHOS"]
    # Unacknowledged, it is delivered again; acknowledged, it is gone.
    assert [c["id"] for c in sync(server)["commands"]] == [cmd["id"]]
    threading.Thread(target=sync, args=(server,), kwargs={"results": [{"id": cmd["id"]}]}, daemon=True).start()
    time.sleep(0.3)
    assert client.get("/api/status").json()["device"]["pending"] == 0


def test_changes_queue_while_offline_in_order(server):
    client = signed_in(server)
    sync(server, config=CONFIG, status=STATUS)
    client.post("/api/show", json={"text": "SOLD OUT", "align": "center"})
    client.post("/api/config", json={"mode": "single:tiktok", "platforms": {"tiktok": {"enabled": False}}})
    client.post("/api/rehome", json={})
    cfg = client.get("/api/config").json()
    assert cfg["text"] == "SOLD OUT" and cfg["mode"] == "single" and cfg["single"] == "tiktok"
    assert cfg["platforms"]["tiktok"] == {"enabled": False, "source": "api", "manual_count": 0}
    commands = sync(server)["commands"]
    assert [c["type"] for c in commands] == ["show", "config", "rehome"]
    assert [c["id"] for c in commands] == sorted(c["id"] for c in commands)


def test_rejected_command_is_reported_and_mirror_follows_the_display(server):
    client = signed_in(server)
    sync(server, config=CONFIG, status=STATUS)
    client.post("/api/config", json={"random_seconds": 1})
    (cmd,) = sync(server)["commands"]
    threading.Thread(target=sync, args=(server,), daemon=True,
                     kwargs={"results": [{"id": cmd["id"], "error": "random_seconds must be an integer 20..3600"}], "config": CONFIG}).start()
    time.sleep(0.3)
    s = client.get("/api/status").json()["device"]
    assert s["pending"] == 0 and s["errors"][-1]["type"] == "config" and "20..3600" in s["errors"][-1]["error"]
    assert "random_seconds" not in client.get("/api/config").json()


def test_link_settings_cannot_be_changed_through_the_server(server):
    client = signed_in(server)
    for key in ["admin_key", "remote_url", "remote_token"]:
        r = client.post("/api/config", json={key: "https://evil.example"})
        assert r.status_code == 400 and key in r.json()["error"]
    assert sync(server) == {"commands": []}


def test_posts_must_be_json(server):
    client = signed_in(server)
    # what a form on another website could send with the visitor's cookie
    assert client.post("/api/show", content='{"text":"HACKED"}', headers={"Content-Type": "text/plain"}).status_code == 400
    for sneaky in ["text/plain; application/json", "text/plain;charset=application/json", "application/jsonx"]:
        assert client.post("/api/show", content='{"text":"HACKED"}', headers={"Content-Type": sneaky}).status_code == 400, sneaky
    cross = {"Content-Type": "application/json", "Sec-Fetch-Site": "cross-site"}
    assert client.post("/api/show", content='{"text":"HACKED"}', headers=cross).status_code == 400
    assert client.post("/api/config", content="[1,2]", headers={"Content-Type": "application/json"}).status_code == 400
    assert client.post("/api/config", content="x" * 40000, headers={"Content-Type": "application/json"}).status_code == 400
    assert sync(server) == {"commands": []}  # none of the above got through
    # what the app itself sends is accepted
    ok = {"Content-Type": "application/json; charset=utf-8", "Sec-Fetch-Site": "same-origin"}
    assert client.post("/api/rehome", content="{}", headers=ok).status_code == 200
    assert [c["type"] for c in sync(server)["commands"]] == ["rehome"]


def test_queue_is_bounded(server):
    client = signed_in(server)
    for i in range(50):
        assert client.post("/api/show", json={"text": str(i)}).status_code == 200
    assert client.post("/api/show", json={"text": "one too many"}).status_code == 429


def test_tokens_pass_through_but_never_touch_disk(server):
    client = signed_in(server)
    sync(server, config=CONFIG, status=STATUS)
    secret = "IGQVJ-super-secret-token"
    client.post("/api/tokens", json={"platform": "instagram", "access_token": secret})
    client.post("/api/show", json={"text": "KEPT"})
    assert secret not in (server.data / "state.json").read_text(encoding="utf-8")
    assert sync(server)["commands"][0]["body"]["access_token"] == secret  # delivered from memory
    # After a restart the token command is gone; ordinary changes survive.
    server.stop()
    server.start()
    assert [c["type"] for c in sync(server)["commands"]] == ["show"]
    assert secret not in json.dumps(signed_in(server).get("/api/status").json())


def test_state_survives_a_restart_and_ids_keep_growing(server):
    client = signed_in(server)
    sync(server, config=CONFIG, status=STATUS)
    client.post("/api/show", json={"text": "ONE"})
    first = sync(server)["commands"][0]["id"]
    server.stop()
    server.start()
    client = signed_in(server)
    assert client.get("/api/config").json()["text"] == "ONE"
    client.post("/api/show", json={"text": "TWO"})
    ids = [c["id"] for c in sync(server)["commands"]]
    assert ids[0] == first and ids[1] > first
