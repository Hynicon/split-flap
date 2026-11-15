"""The first-run setup page and the server's own settings (account, device token, name, time zone).

    pytest server/tests
"""

from __future__ import annotations

import json
import re

import httpx
import pytest

from test_server import Server

PW = "a long enough password"


@pytest.fixture
def fresh(tmp_path):
    """A server with no APP_PASSWORD or DEVICE_TOKEN: it must be set up in the browser."""
    s = Server(tmp_path, APP_PASSWORD="", DEVICE_TOKEN="")
    s.start()
    yield s
    s.stop()


def set_up(s: Server, **extra) -> tuple[httpx.Client, str]:
    client = httpx.Client(base_url=s.url, timeout=10)
    form = {"username": "trailer", "password": PW, "password2": PW, "display_name": "Taco Truck", "time_zone": "Europe/Lisbon", **extra}
    r = client.post("/setup", data=form)
    assert r.status_code == 200, r.text
    token = re.search(r'class="token" readonly value="([0-9a-f]{48})"', r.text).group(1)
    return client, token


def test_everything_leads_to_setup_until_done(fresh):
    anon = httpx.Client(base_url=fresh.url)
    for path in ["/", "/login", "/index.html"]:
        r = anon.get(path)
        assert r.status_code == 303 and r.headers["location"] == "/setup", path
    assert anon.get("/api/status").status_code == 503
    assert anon.get("/setup").status_code == 200
    r = httpx.post(fresh.url + "/api/device/sync", json={}, headers={"Authorization": "Bearer " + "0" * 48})
    assert r.status_code == 401


def test_setup_creates_the_account_and_a_device_token(fresh):
    client, token = set_up(fresh)
    # signed in straight away
    status = client.get("/api/status").json()
    assert status["server"] == {"name": "Taco Truck", "time_zone": "Europe/Lisbon"}
    # the token works for the display, and isn't stored in plain text
    r = httpx.post(fresh.url + "/api/device/sync", json={"status": {}}, headers={"Authorization": f"Bearer {token}"}, timeout=10)
    assert r.status_code == 200
    auth = (fresh.data / "auth.json").read_text()
    assert token not in auth and PW not in auth
    # setup is gone now: a second visitor can't take over
    other = httpx.Client(base_url=fresh.url)
    assert other.get("/setup").headers["location"] == "/login"
    r = other.post("/setup", data={"username": "evil", "password": "x" * 20, "password2": "x" * 20})
    assert r.status_code == 303 and r.headers["location"] == "/login"
    assert json.loads(auth)["username"] == "trailer"
    # sign in with the new account
    assert other.post("/login", data={"username": "trailer", "password": PW}).status_code == 303
    assert other.get("/api/status").status_code == 200


def test_setup_checks_its_input(fresh):
    c = httpx.Client(base_url=fresh.url)
    bad = [
        {"username": "trailer", "password": "short", "password2": "short"},
        {"username": "trailer", "password": PW, "password2": PW + "x"},
        {"username": "has space", "password": PW, "password2": PW},
        {"username": "trailer", "password": PW, "password2": PW, "time_zone": "<script>"},
    ]
    for form in bad:
        assert c.post("/setup", data=form).status_code == 400, form
    assert c.post("/setup", data={"username": "trailer", "password": PW, "password2": PW},
                  headers={"Sec-Fetch-Site": "cross-site"}).status_code == 403
    assert c.get("/setup").status_code == 200  # still not set up


def test_name_is_escaped(fresh):
    client, _ = set_up(fresh, display_name="<b>Tacos</b>")
    client.post("/logout")
    page = client.get("/login").text
    assert "<b>Tacos</b>" not in page and "&lt;b&gt;Tacos&lt;/b&gt;" in page


def test_change_password_signs_out_other_phones(fresh):
    phone1, _ = set_up(fresh)
    phone2 = httpx.Client(base_url=fresh.url, timeout=10)
    assert phone2.post("/login", data={"username": "trailer", "password": PW}).status_code == 303
    new = "an even longer password"
    r = phone1.post("/api/server", json={"current_password": "wrong one", "new_password": new})
    assert r.status_code == 403
    r = phone1.post("/api/server", json={"current_password": PW, "new_password": new})
    assert r.status_code == 200 and r.json()["signed_out_others"]
    assert phone1.get("/api/status").status_code == 200  # this phone got a new session
    assert phone2.get("/api/status").status_code == 401  # the other one is signed out
    assert phone2.post("/login", data={"username": "trailer", "password": PW}).status_code == 401
    assert phone2.post("/login", data={"username": "trailer", "password": new}).status_code == 303


def test_new_device_token_replaces_the_old_one(fresh):
    client, old = set_up(fresh)
    assert client.post("/api/server/device-token", json={"current_password": "nope"}).status_code == 403
    r = client.post("/api/server/device-token", json={"current_password": PW})
    new = r.json()["device_token"]
    sync = lambda t: httpx.post(fresh.url + "/api/device/sync", json={"status": {}}, headers={"Authorization": f"Bearer {t}"}, timeout=10).status_code
    assert sync(old) == 401
    assert sync(new) == 200


def test_name_and_time_zone_need_no_password(fresh):
    client, _ = set_up(fresh)
    r = client.post("/api/server", json={"display_name": "Burritos", "time_zone": "America/New_York"})
    assert r.status_code == 200
    assert client.get("/api/status").json()["server"] == {"name": "Burritos", "time_zone": "America/New_York"}
    assert client.post("/api/server", json={"time_zone": "not a zone!"}).status_code == 400
    # the account is untouched without the password
    assert client.post("/api/server", json={"username": "someone"}).status_code == 403
    assert client.get("/api/server").json()["username"] == "trailer"


def test_environment_variables_skip_setup(tmp_path):
    s = Server(tmp_path)  # APP_PASSWORD and DEVICE_TOKEN set, as before
    s.start()
    try:
        c = httpx.Client(base_url=s.url, timeout=10)
        assert c.get("/setup").headers["location"] == "/login"
        assert c.post("/login", data={"username": "admin", "password": "correct horse battery"}).status_code == 303
        info = c.get("/api/server").json()
        assert info["password_from_env"] and info["device_token_from_env"]
        assert c.post("/api/server", json={"current_password": "correct horse battery", "new_password": "x" * 20}).status_code == 400
        assert c.post("/api/server/device-token", json={"current_password": "correct horse battery"}).status_code == 400
    finally:
        s.stop()
