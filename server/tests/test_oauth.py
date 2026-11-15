"""Connecting Instagram, Facebook and TikTok from the app, against a fake platform server.

    pytest server/tests
"""

from __future__ import annotations

import json
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import httpx
import pytest

from test_server import Server, free_port
from test_users import STAFF_PW, login
from test_setup import set_up

PAGES = [{"id": "111", "name": "Taco Truck", "access_token": "page-token-111"}]


class FakePlatforms(BaseHTTPRequestHandler):
    pages = PAGES

    def log_message(self, *args):
        pass

    def reply(self, doc, code=200):
        body = json.dumps(doc).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path, _, query = self.path.partition("?")
        q = dict(urllib.parse.parse_qsl(query))
        if path == "/ig_graph/access_token":
            return self.reply({"access_token": "ig-long", "expires_in": 5184000})
        if path == "/ig_graph/v26.0/me":
            return self.reply({"username": "tacotruck", "followers_count": 1234})
        if path == "/fb_graph/oauth/access_token":
            return self.reply({"access_token": "fb-long" if q.get("grant_type") else "fb-user"})
        if path == "/fb_graph/me/accounts":
            return self.reply({"data": type(self).pages})
        if path.startswith("/fb_graph/"):
            return self.reply({"name": "Taco Truck", "followers_count": 77})
        if path == "/tt_api/user/info/":
            return self.reply({"data": {"user": {"display_name": "tacos", "follower_count": 99}}})
        self.reply({"error": {"message": "unknown " + path}}, 404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        form = dict(urllib.parse.parse_qsl(self.rfile.read(length).decode()))
        if self.path == "/ig_token":
            if form.get("code") != "good":
                return self.reply({"error_type": "OAuthException", "error_message": "Invalid code"}, 400)
            return self.reply({"access_token": "ig-short", "user_id": 1})
        if self.path == "/tt_api/oauth/token/":
            return self.reply({"access_token": "tt-access", "refresh_token": "tt-refresh", "expires_in": 86400,
                               "refresh_expires_in": 31536000, "scope": "user.info.basic,user.info.stats"})
        self.reply({"error": "unknown"}, 404)


@pytest.fixture
def server(tmp_path):
    fake = ThreadingHTTPServer(("127.0.0.1", free_port()), FakePlatforms)
    threading.Thread(target=fake.serve_forever, daemon=True).start()
    s = Server(tmp_path, APP_PASSWORD="", DEVICE_TOKEN="", OAUTH_TEST_BASE=f"http://127.0.0.1:{fake.server_port}")
    s.start()
    yield s
    s.stop()
    fake.shutdown()
    FakePlatforms.pages = PAGES


def connect(client: httpx.Client, platform: str, code: str = "good") -> httpx.Response:
    """Press Connect, 'log in' on the platform, and follow its redirect back to this server."""
    r = client.get(f"/oauth/{platform}/start")
    assert r.status_code == 303, r.text
    state = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(r.headers["location"]).query))["state"]
    return client.get(f"/oauth/{platform}/callback", params={"code": code, "state": state})


def queued_tokens(s: Server, token: str) -> list[dict]:
    r = httpx.post(s.url + "/api/device/sync", json={}, headers={"Authorization": f"Bearer {token}"}, timeout=10)
    return [c["body"] for c in r.json()["commands"] if c["type"] == "tokens"]


def save_app(client: httpx.Client, platform: str) -> None:
    r = client.post("/api/integrations", json={"platform": platform, "app_id": f"{platform}-app", "app_secret": "s3cret"})
    assert r.status_code == 200, r.text


def test_app_credentials_are_kept_but_secrets_never_shown(server):
    admin, _ = set_up(server)
    save_app(admin, "instagram")
    info = admin.get("/api/integrations").json()["instagram"]
    assert info["app_id"] == "instagram-app" and info["secret_set"] and "s3cret" not in json.dumps(info)
    assert info["redirect_uri"] == server.url + "/oauth/instagram/callback"
    # leaving the secret out keeps it
    admin.post("/api/integrations", json={"platform": "instagram", "app_id": "new-id"})
    assert admin.get("/api/integrations").json()["instagram"]["secret_set"]
    assert admin.post("/api/integrations", json={"platform": "myspace", "app_id": "x"}).status_code == 400


def test_instagram_connects_and_queues_tokens(server):
    admin, token = set_up(server)
    save_app(admin, "instagram")
    r = connect(admin, "instagram")
    assert r.status_code == 303 and r.headers["location"] == "/?connected=instagram"
    last = admin.get("/api/integrations").json()["instagram"]["last"]
    assert last["ok"] and last["account"] == "@tacotruck" and last["followers"] == 1234
    [payload] = queued_tokens(server, token)
    assert payload["platform"] == "instagram" and payload["access_token"] == "ig-long"
    assert "ig-long" not in (server.data / "state.json").read_text()  # tokens stay in memory


def test_tiktok_and_single_page_facebook(server):
    admin, token = set_up(server)
    for platform in ("tiktok", "facebook"):
        save_app(admin, platform)
        assert connect(admin, platform).status_code == 303
    payloads = {p["platform"]: p for p in queued_tokens(server, token)}
    assert payloads["tiktok"]["refresh_token"] == "tt-refresh" and payloads["tiktok"]["client_key"] == "tiktok-app"
    assert payloads["facebook"]["page_id"] == "111" and payloads["facebook"]["access_token"] == "page-token-111"


def test_facebook_asks_which_page(server):
    FakePlatforms.pages = PAGES + [{"id": "222", "name": "Burrito Bus", "access_token": "page-token-222"}]
    admin, token = set_up(server)
    save_app(admin, "facebook")
    r = connect(admin, "facebook")
    assert r.status_code == 200 and "Burrito Bus" in r.text
    pending = r.text.split('name="pending" value="')[1].split('"')[0]
    r = admin.post("/oauth/facebook/page", data={"pending": pending, "page": "222"})
    assert r.status_code == 303
    [payload] = queued_tokens(server, token)
    assert payload["page_id"] == "222"
    # the choice works once only
    assert admin.post("/oauth/facebook/page", data={"pending": pending, "page": "111"}).status_code == 303
    assert not admin.get("/api/integrations").json()["facebook"]["last"]["ok"]


def test_bad_state_code_and_staff_are_refused(server):
    admin, token = set_up(server)
    save_app(admin, "instagram")
    r = admin.get("/oauth/instagram/callback", params={"code": "good", "state": "made-up"})
    assert r.status_code == 303 and not admin.get("/api/integrations").json()["instagram"]["last"]["ok"]
    # a state works once
    r = admin.get("/oauth/instagram/start")
    state = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(r.headers["location"]).query))["state"]
    admin.get("/oauth/instagram/callback", params={"code": "bad", "state": state})
    last = admin.get("/api/integrations").json()["instagram"]["last"]
    assert not last["ok"] and "Invalid code" in last["message"]
    admin.get("/oauth/instagram/callback", params={"code": "good", "state": state})
    assert "expired" in admin.get("/api/integrations").json()["instagram"]["last"]["message"]
    assert queued_tokens(server, token) == []
    # the platform said no
    r = admin.get("/oauth/instagram/start")
    state = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(r.headers["location"]).query))["state"]
    admin.get("/oauth/instagram/callback", params={"error": "access_denied", "state": state})
    assert "access_denied" in admin.get("/api/integrations").json()["instagram"]["last"]["message"]
    # staff can't connect or see the app credentials
    admin.post("/api/users", json={"username": "maria", "password": STAFF_PW, "role": "staff"})
    staff = login(server, "maria", STAFF_PW)
    assert staff.get("/oauth/instagram/start").headers["location"] == "/?connected=instagram"
    assert staff.get("/api/integrations").status_code == 403
    # without credentials Connect explains instead of failing
    assert admin.get("/oauth/tiktok/start").headers["location"] == "/?connected=tiktok"
    assert "ID and secret" in admin.get("/api/integrations").json()["tiktok"]["last"]["message"]
