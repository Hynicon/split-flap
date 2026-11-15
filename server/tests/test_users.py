"""Several accounts: admins and staff.

    pytest server/tests
"""

from __future__ import annotations

import json

import httpx
import pytest

from test_server import Server, sync
from test_setup import PW, set_up

STAFF_PW = "staff password 123"


@pytest.fixture
def fresh(tmp_path):
    s = Server(tmp_path, APP_PASSWORD="", DEVICE_TOKEN="")
    s.start()
    yield s
    s.stop()


def login(s: Server, username: str, password: str) -> httpx.Client:
    c = httpx.Client(base_url=s.url, timeout=10)
    r = c.post("/login", data={"username": username, "password": password})
    assert r.status_code == 303, r.text
    return c


def with_staff(s: Server) -> tuple[httpx.Client, httpx.Client, str]:
    admin, token = set_up(s)
    r = admin.post("/api/users", json={"username": "Maria", "password": STAFF_PW, "role": "staff"})
    assert r.status_code == 200, r.text
    return admin, login(s, "maria", STAFF_PW), token


def test_admin_adds_lists_and_removes_users(fresh):
    admin, staff, _ = with_staff(fresh)
    users = admin.get("/api/users").json()["users"]
    assert [(u["username"], u["role"], u["you"]) for u in users] == [("Maria", "staff", False), ("trailer", "admin", True)]
    assert admin.post("/api/users", json={"username": "maria", "password": STAFF_PW}).status_code == 409  # taken
    assert admin.post("/api/users", json={"username": "x", "password": "short"}).status_code == 400
    assert admin.post("/api/users", json={"username": "x y", "password": STAFF_PW}).status_code == 400
    assert admin.post("/api/users", json={"username": "x", "password": STAFF_PW, "role": "owner"}).status_code == 400
    assert admin.delete("/api/users/maria").status_code == 200
    assert staff.get("/api/status").status_code == 401  # removed: signed out
    assert admin.delete("/api/users/nobody").status_code == 404


def test_staff_can_change_what_is_shown_only(fresh):
    admin, staff, token = with_staff(fresh)
    httpx.post(fresh.url + "/api/device/sync", json={"config": {"mode": "cycle"}, "status": {}},
               headers={"Authorization": f"Bearer {token}"}, timeout=10)
    assert staff.get("/api/status").json()["me"] == {"username": "Maria", "role": "staff"}
    assert staff.get("/api/config").status_code == 200
    assert staff.post("/api/show", json={"text": "TACOS"}).status_code == 200
    assert staff.post("/api/config", json={"mode": "random", "words": ["A"], "countdown_at": "21:00"}).status_code == 200
    for body in [{"units": 7}, {"timezone": "UTC0"}, {"mode": "text", "speed": 5}]:
        r = staff.post("/api/config", json=body)
        assert r.status_code == 403 and "admin" in r.json()["error"], body
    for path in ["/api/rehome", "/api/tokens", "/api/server", "/api/server/device-token", "/api/users"]:
        assert staff.post(path, json={"platform": "tiktok", "access_token": "x"}).status_code == 403, path
    for path in ["/api/server", "/api/users"]:
        assert staff.get(path).status_code == 403, path
    assert staff.delete("/api/users/trailer").status_code == 403
    # the admin can do all of it
    assert admin.post("/api/config", json={"units": 7}).status_code == 200
    assert admin.post("/api/rehome", json={}).status_code == 200


def test_staff_changes_own_password(fresh):
    admin, staff, _ = with_staff(fresh)
    new = "a new staff password"
    assert staff.post("/api/me", json={"current_password": "wrong", "new_password": new}).status_code == 403
    r = staff.post("/api/me", json={"current_password": STAFF_PW, "new_password": new})
    assert r.status_code == 200 and r.json()["role"] == "staff"
    assert staff.get("/api/status").status_code == 200
    login(fresh, "maria", new)
    assert admin.get("/api/status").status_code == 200  # nobody else is signed out


def test_reset_password_signs_out_only_that_person(fresh):
    admin, staff, _ = with_staff(fresh)
    temp = "temporary password 1"
    assert admin.post("/api/users/maria", json={"password": temp}).status_code == 200
    assert staff.get("/api/status").status_code == 401
    assert admin.get("/api/status").status_code == 200
    login(fresh, "maria", temp)


def test_there_is_always_an_admin(fresh):
    admin, staff, _ = with_staff(fresh)
    assert admin.post("/api/users/trailer", json={"role": "staff"}).status_code == 400
    assert admin.delete("/api/users/trailer").status_code == 400
    assert admin.post("/api/users/maria", json={"role": "admin"}).status_code == 200
    # two admins now: one may step down
    assert admin.post("/api/users/trailer", json={"role": "staff"}).status_code == 200
    assert admin.get("/api/users").status_code == 403  # no longer an admin
    assert staff.get("/api/users").status_code == 200  # Maria is


def test_admin_header_for_scripts(fresh):
    admin, staff, _ = with_staff(fresh)
    assert httpx.get(fresh.url + "/api/status", headers={"X-Admin-Key": PW}).status_code == 200
    assert httpx.get(fresh.url + "/api/status", headers={"X-Admin-Key": STAFF_PW}).status_code == 401  # staff can't


def test_old_single_account_file_becomes_an_admin(tmp_path):
    import hashlib
    import secrets
    salt = secrets.token_bytes(16)
    digest = hashlib.scrypt(PW.encode(), salt=salt, n=2**14, r=8, p=1, dklen=32)
    (tmp_path / "auth.json").write_text(json.dumps({
        "username": "trailer", "password": f"scrypt$16384$8$1${salt.hex()}${digest.hex()}",
        "session_epoch": "abc", "display_name": "Old"}))
    s = Server(tmp_path, APP_PASSWORD="", DEVICE_TOKEN="")
    s.start()
    try:
        c = login(s, "trailer", PW)
        assert c.get("/api/status").json()["me"] == {"username": "trailer", "role": "admin"}
        assert c.get("/api/status").json()["server"]["name"] == "Old"
    finally:
        s.stop()


def test_environment_admin_plus_stored_users(tmp_path):
    s = Server(tmp_path)  # APP_PASSWORD set: "admin" lives in the environment
    s.start()
    try:
        env = login(s, "admin", "correct horse battery")
        assert env.post("/api/users", json={"username": "maria", "password": STAFF_PW, "role": "staff"}).status_code == 200
        login(s, "maria", STAFF_PW)
        users = env.get("/api/users").json()["users"]
        assert users[0]["env"] and users[0]["username"] == "admin"
        assert env.delete("/api/users/admin").status_code == 400  # can't remove the environment's admin
        sync(s)  # the device token from the environment still works
    finally:
        s.stop()
