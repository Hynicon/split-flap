"""Relay server for the split-flap display.

The display sits behind a network that nothing can connect in to (a phone hotspot), so it calls
out to this server. The server:

- serves the phone app (the same files the ESP32 serves, from firmware/master/data);
- puts a login in front of everything;
- queues the changes made in the app until the display collects them (POST /api/device/sync,
  which the display holds open so a change arrives within about a second);
- remembers the status and settings the display last reported, so the app has something to show.

The display stays the source of truth: the server never decides what is shown.

Environment: APP_PASSWORD (login), DEVICE_TOKEN (the display's token), DATA_DIR, STATIC_DIR,
SECRET_KEY (optional; generated and kept in DATA_DIR otherwise), TRUST_PROXY=1 when the server is
only reachable through a reverse proxy (the Docker image sets it).
"""

from __future__ import annotations

import asyncio
from collections import OrderedDict, deque
import ipaddress
import copy
import hashlib
import hmac
import json
import os
import secrets
import time
import urllib.parse
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, RedirectResponse, Response

APP_PASSWORD = os.environ.get("APP_PASSWORD", "")
DEVICE_TOKEN = os.environ.get("DEVICE_TOKEN", "")
DATA_DIR = Path(os.environ.get("DATA_DIR", "/data"))
STATIC_DIR = Path(os.environ.get("STATIC_DIR", Path(__file__).resolve().parent.parent / "firmware" / "master" / "data"))
TRUST_PROXY = os.environ.get("TRUST_PROXY", "") == "1"  # set only when every request comes through the proxy
HOLD_SECONDS = float(os.environ.get("HOLD_SECONDS", "25"))  # how long a sync request waits for a command
ONLINE_SECONDS = HOLD_SECONDS + 20  # no sync for this long means the display is offline
SESSION_DAYS = 30
MAX_BODY = 32 * 1024
MAX_PENDING = 50
# Settings that would cut the link or hand over the display may only be changed on the display itself.
LOCAL_ONLY = ("admin_key", "remote_url", "remote_token")

if len(APP_PASSWORD) < 8:
    raise SystemExit("Set APP_PASSWORD to at least 8 characters.")
if len(DEVICE_TOKEN) < 16:
    raise SystemExit("Set DEVICE_TOKEN to at least 16 characters (for example: openssl rand -hex 24).")

DATA_DIR.mkdir(parents=True, exist_ok=True)
STATE_FILE = DATA_DIR / "state.json"
KEY_FILE = DATA_DIR / "secret.key"


def _secret_key() -> bytes:
    if os.environ.get("SECRET_KEY"):
        return os.environ["SECRET_KEY"].encode()
    if not KEY_FILE.exists():
        KEY_FILE.write_text(secrets.token_hex(32))
        try:
            KEY_FILE.chmod(0o600)
        except OSError:
            pass
    return KEY_FILE.read_text().strip().encode()


SECRET = _secret_key()


class State:
    """What the display last reported, and the commands waiting for it."""

    def __init__(self) -> None:
        self.config: dict | None = None
        self.status: dict | None = None
        self.last_seen = 0.0
        self.pending: list[dict] = []
        self.errors: list[dict] = []  # the last few commands the display rejected
        self.last_id = 0
        self.holding = 0  # sync requests held open right now
        self.wake = asyncio.Event()
        self.load()

    def load(self) -> None:
        try:
            saved = json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return
        self.config = saved.get("config")
        self.status = saved.get("status")
        self.last_seen = saved.get("last_seen", 0.0)
        self.pending = saved.get("pending", [])
        self.last_id = saved.get("last_id", 0)

    def save(self) -> None:
        # Token commands stay in memory only: tokens are never written to this server's disk.
        doc = {
            "config": self.config,
            "status": self.status,
            "last_seen": self.last_seen,
            "pending": [c for c in self.pending if c["type"] != "tokens"],
            "last_id": self.last_id,
        }
        tmp = STATE_FILE.with_suffix(".tmp")
        tmp.write_text(json.dumps(doc), encoding="utf-8")
        tmp.replace(STATE_FILE)

    def enqueue(self, kind: str, body: dict) -> None:
        # Ids only ever grow, across restarts too, so the display can tell a repeat from a new command.
        self.last_id = max(self.last_id + 1, int(time.time() * 1000))
        self.pending.append({"id": self.last_id, "type": kind, "body": body})
        self.save()
        self.wake.set()

    @property
    def online(self) -> bool:
        return self.holding > 0 or time.time() - self.last_seen < ONLINE_SECONDS

    def effective_config(self) -> dict | None:
        """The display's settings with the queued changes laid over them, so the app shows an edit at once."""
        if self.config is None:
            return None
        cfg = copy.deepcopy(self.config)
        for cmd in self.pending:
            if cmd["type"] == "config":
                merge(cfg, cmd["body"])
            elif cmd["type"] == "show":
                cfg["mode"] = "text"
                cfg["text"] = cmd["body"].get("text", "")
                if cmd["body"].get("align"):
                    cfg["text_align"] = cmd["body"]["align"]
        return cfg


def merge(base: dict, patch: dict) -> None:
    for key, value in patch.items():
        if key == "platforms" and isinstance(value, dict):
            for platform, fields in value.items():
                if isinstance(fields, dict):
                    base.setdefault("platforms", {}).setdefault(platform, {}).update(fields)
        elif key == "mode" and isinstance(value, str) and ":" in value:  # "single:tiktok"
            base["mode"], base["single"] = value.split(":", 1)
        else:
            base[key] = value


app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
state = State()

# ---------------------------------------------------------------- login


def sign(expires: int) -> str:
    mac = hmac.new(SECRET, str(expires).encode(), hashlib.sha256).hexdigest()
    return f"{expires}.{mac}"


def valid_session(cookie: str | None) -> bool:
    try:
        expires, mac = (cookie or "").split(".", 1)
        return hmac.compare_digest(sign(int(expires)), f"{expires}.{mac}") and int(expires) > time.time()
    except ValueError:
        return False


def same(a: str, b: str) -> bool:
    return hmac.compare_digest(a.encode(), b.encode())


def client_ip(request: Request) -> str:
    """The address to count wrong passwords against. Behind the proxy that is the last entry of
    X-Forwarded-For, the one the proxy itself added: earlier entries come from the client and can
    be made up. Without a proxy (TRUST_PROXY unset) the header is ignored altogether."""
    if TRUST_PROXY:
        last = request.headers.get("x-forwarded-for", "").split(",")[-1].strip()
        if last:
            return last
    return request.client.host if request.client else "?"


def is_https(request: Request) -> bool:
    return request.headers.get("x-forwarded-proto", request.url.scheme) == "https"


# Wrong passwords, from the sign-in form and from the X-Admin-Key header alike, are counted per
# network rather than per address (an IPv4 /24 or an IPv6 /64), so changing address within one
# network doesn't buy more guesses. Five in five minutes lock that network out. Every wrong password
# also waits 1 s before the answer.
#
# Across all networks there is no hard lockout, because anyone could then keep the owner from
# signing in. Instead, once more than ATTACK_FAILS wrong passwords arrive within the window, a
# network with two wrong passwords is locked out, so each new network an attacker brings gets two
# guesses instead of five.
#
# At most MAX_NETWORKS guessing networks are tracked one by one, and live entries are never
# dropped. When the table is full, further networks share OVERFLOW_BUCKETS hashed counters and are
# held to the stricter ATTACK_LOCK limit; two networks rarely share a counter, so a newcomer such as
# the owner can still sign in. Trusted networks are always tracked one by one.
#
# Phone carriers put many customers behind shared addresses, so the owner may share a network with
# a guesser. Networks that signed in successfully in the last 30 days are therefore exempt from the
# attack rule, and a valid session cookie is checked before any of this, so someone already signed
# in is never locked out.
FAILS: OrderedDict[str, list[float]] = OrderedDict()  # network -> times of recent wrong passwords, oldest first
ALL_FAILS: deque[float] = deque()  # times of every recent wrong password
TRUSTED: OrderedDict[str, float] = OrderedDict()  # network -> time of its last good sign-in
MAX_FAILS, ATTACK_FAILS, ATTACK_LOCK, FAIL_WINDOW = 5, 30, 2, 300
MAX_NETWORKS, TRUST_SECONDS = int(os.environ.get("MAX_NETWORKS", "20000")), 30 * 86400
_warned_at = _pruned_at = 0.0


def fail_key(ip: str) -> str:
    try:
        return str(ipaddress.ip_network(f"{ip}/{24 if ':' not in ip else 64}", strict=False))
    except ValueError:
        return ip


def recent_fails(key: str) -> int:
    recent = [t for t in FAILS.get(key, []) if time.time() - t < FAIL_WINDOW]
    if recent:
        FAILS[key] = recent
    else:
        FAILS.pop(key, None)
    return len(recent)


def under_attack() -> bool:
    while ALL_FAILS and time.time() - ALL_FAILS[0] >= FAIL_WINDOW:
        ALL_FAILS.popleft()
    return len(ALL_FAILS) >= ATTACK_FAILS


def trusted(key: str) -> bool:
    t = TRUSTED.get(key)
    return t is not None and time.time() - t < TRUST_SECONDS


OVERFLOW_BUCKETS = 65536
OVERFLOW: dict[int, list[float]] = {}  # bucket -> times of recent wrong passwords


def table_full() -> bool:
    return len(FAILS) >= MAX_NETWORKS


def overflow_bucket(key: str) -> int | None:
    """The shared counter for a network that doesn't fit in FAILS, or None if it is tracked there."""
    if key in FAILS or not table_full() or trusted(key):
        return None
    return hash(key) % OVERFLOW_BUCKETS  # str hashes are randomised per process, so collisions can't be aimed


def overflow_fails(bucket: int) -> int:
    recent = [t for t in OVERFLOW.get(bucket, []) if time.time() - t < FAIL_WINDOW]
    if recent:
        OVERFLOW[bucket] = recent
    else:
        OVERFLOW.pop(bucket, None)
    return len(recent)


def locked_out(ip: str) -> bool:
    key = fail_key(ip)
    bucket = overflow_bucket(key)
    if bucket is not None:
        return overflow_fails(bucket) >= ATTACK_LOCK
    n = recent_fails(key)
    return n >= MAX_FAILS or (n >= ATTACK_LOCK and under_attack() and not trusted(key))


def good_password(ip: str) -> None:
    key = fail_key(ip)
    TRUSTED.pop(key, None)
    TRUSTED[key] = time.time()
    while len(TRUSTED) > 1000:
        TRUSTED.popitem(last=False)


def wrong_password(ip: str) -> None:
    global _warned_at, _pruned_at
    now = time.time()
    key = fail_key(ip)
    ALL_FAILS.append(now)
    if table_full() and now - _pruned_at > 10:  # prune at most every 10 s
        _pruned_at = now
        for k in list(FAILS):
            recent_fails(k)
        for b in list(OVERFLOW):
            overflow_fails(b)
    bucket = overflow_bucket(key)
    if bucket is None:
        FAILS[key] = FAILS.pop(key, []) + [now]
    else:
        OVERFLOW.setdefault(bucket, []).append(now)
    if under_attack() and now - _warned_at > FAIL_WINDOW:
        _warned_at = now
        print(f"WARNING: {len(ALL_FAILS)} wrong passwords in the last {FAIL_WINDOW // 60} minutes "
              f"from {len(FAILS)} networks", flush=True)


async def logged_in(request: Request) -> bool:
    if valid_session(request.cookies.get("session")):
        return True
    # Scripts (tools/auth) send the password in the header the display's own API uses.
    key = request.headers.get("x-admin-key", "")
    if not key:
        return False
    ip = client_ip(request)
    if locked_out(ip):
        return False
    if same(key, APP_PASSWORD):
        good_password(ip)
        return True
    wrong_password(ip)
    await asyncio.sleep(1)
    return False


LOGIN_PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Split-Flap: sign in</title><link rel="stylesheet" href="/style.css"></head>
<body class="login"><main><form method="post" action="/login" class="card">
<h1>Split-Flap</h1>
<label>Password <input name="password" type="password" autocomplete="current-password" autofocus required></label>
<p class="error" role="alert">__ERROR__</p>
<button type="submit">Sign in</button>
</form></main></body></html>"""

PUBLIC_PATHS = {"/login", "/style.css", "/healthz", "/api/device/sync"}


@app.middleware("http")
async def require_login(request: Request, call_next):
    path = request.url.path
    if path in PUBLIC_PATHS or await logged_in(request):
        return await call_next(request)
    if path.startswith("/api/"):
        return JSONResponse({"error": "not signed in"}, status_code=401)
    return RedirectResponse("/login", status_code=303)


@app.get("/login")
async def login_page(request: Request):
    if await logged_in(request):
        return RedirectResponse("/", status_code=303)
    return HTMLResponse(LOGIN_PAGE.replace("__ERROR__", ""), headers={"Cache-Control": "no-store"})


@app.post("/login")
async def login(request: Request):
    ip = client_ip(request)
    if locked_out(ip):
        page = LOGIN_PAGE.replace("__ERROR__", "Too many wrong passwords from your network. Wait five minutes and try again.")
        return HTMLResponse(page, status_code=429)
    raw = (await request.body())[:4096].decode("utf-8", "replace")
    password = urllib.parse.parse_qs(raw, keep_blank_values=True).get("password", [""])[0]
    if locked_out(ip):  # again: other guesses may have landed while the body was read
        page = LOGIN_PAGE.replace("__ERROR__", "Too many wrong passwords from your network. Wait five minutes and try again.")
        return HTMLResponse(page, status_code=429)
    if not same(password, APP_PASSWORD):
        wrong_password(ip)
        await asyncio.sleep(1)
        return HTMLResponse(LOGIN_PAGE.replace("__ERROR__", "Wrong password."), status_code=401)
    good_password(ip)
    res = RedirectResponse("/", status_code=303)
    res.set_cookie("session", sign(int(time.time()) + SESSION_DAYS * 86400), max_age=SESSION_DAYS * 86400,
                   httponly=True, secure=is_https(request), samesite="lax")
    return res


@app.post("/logout")
async def logout():
    res = RedirectResponse("/login", status_code=303)
    res.delete_cookie("session")
    return res


@app.get("/healthz")
async def healthz():
    return {"ok": True}


# ---------------------------------------------------------------- the display's side


async def read_json(request: Request) -> dict | None:
    """The request body as a JSON object, or None. Requiring the JSON content type also keeps
    other websites from posting to the API with the visitor's session."""
    # The media type must be exactly application/json: "text/plain; application/json" still counts
    # as text/plain to a browser, which would send it cross-site without asking.
    if request.headers.get("content-type", "").split(";", 1)[0].strip().lower() != "application/json":
        return None
    # Browsers say where a request comes from; anything but this site itself is refused.
    # Scripts and the display don't send this header.
    if request.headers.get("sec-fetch-site", "same-origin") not in ("same-origin", "none"):
        return None
    raw = await request.body()
    if len(raw) > MAX_BODY:
        return None
    try:
        doc = json.loads(raw or b"{}")
    except ValueError:
        return None
    return doc if isinstance(doc, dict) else None


@app.post("/api/device/sync")
async def device_sync(request: Request):
    auth = request.headers.get("authorization", "")
    if not auth.startswith("Bearer ") or not same(auth[7:], DEVICE_TOKEN):
        return JSONResponse({"error": "wrong device token"}, status_code=401)
    doc = await read_json(request)
    if doc is None:
        return JSONResponse({"error": "expected a JSON object"}, status_code=400)

    # Results acknowledge commands: those leave the queue, whether they worked or not.
    results = doc.get("results") if isinstance(doc.get("results"), list) else []
    acked = {r.get("id") for r in results if isinstance(r, dict)}
    for r in results:
        if isinstance(r, dict) and r.get("error"):
            kind = next((c["type"] for c in state.pending if c["id"] == r.get("id")), "command")
            state.errors = (state.errors + [{"type": kind, "error": str(r["error"])[:200], "at": int(time.time())}])[-5:]
    state.pending = [c for c in state.pending if c["id"] not in acked]
    if isinstance(doc.get("status"), dict):
        state.status = doc["status"]
    if isinstance(doc.get("config"), dict):
        state.config = doc["config"]
    state.last_seen = time.time()
    state.save()

    if not state.pending:
        state.wake.clear()
        state.holding += 1
        try:
            await asyncio.wait_for(state.wake.wait(), timeout=HOLD_SECONDS)
        except asyncio.TimeoutError:
            pass
        finally:
            state.holding -= 1
            state.last_seen = time.time()
    return JSONResponse({"commands": state.pending}, headers={"Cache-Control": "no-store"})


# ---------------------------------------------------------------- the app's side (same shape as the display's own API)


def no_store(payload: dict, code: int = 200) -> JSONResponse:
    return JSONResponse(payload, status_code=code, headers={"Cache-Control": "no-store"})


@app.get("/api/status")
async def api_status():
    status = copy.deepcopy(state.status) if state.status else {}
    status["via"] = "server"
    status["device"] = {
        "online": state.online,
        "last_seen": int(state.last_seen),
        "pending": len(state.pending),
        "errors": state.errors,
        "known": state.config is not None,
    }
    return no_store(status)


@app.get("/api/config")
async def api_config():
    cfg = state.effective_config()
    if cfg is None:
        return no_store({"error": "The display has not connected to this server yet."}, 503)
    return no_store(cfg)


async def queue(request: Request, kind: str):
    body = await read_json(request)
    if body is None:
        return no_store({"error": "expected a JSON object"}, 400)
    if kind == "config":
        blocked = [k for k in LOCAL_ONLY if k in body]
        if blocked:
            return no_store({"error": f"{blocked[0]} can only be changed on the display's own page"}, 400)
    if len(state.pending) >= MAX_PENDING:
        return no_store({"error": "Too many changes are waiting for the display. Try again when it is back online."}, 429)
    state.enqueue(kind, body)
    return no_store({"ok": True, "queued": not state.online})


@app.post("/api/config")
async def api_config_post(request: Request):
    return await queue(request, "config")


@app.post("/api/show")
async def api_show(request: Request):
    return await queue(request, "show")


@app.post("/api/rehome")
async def api_rehome(request: Request):
    return await queue(request, "rehome")


@app.post("/api/tokens")
async def api_tokens(request: Request):
    return await queue(request, "tokens")


# ---------------------------------------------------------------- the app's files

TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
         ".webmanifest": "application/manifest+json", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon"}


@app.get("/{path:path}")
async def static(path: str):
    name = path or "index.html"
    file = (STATIC_DIR / name).resolve()
    if STATIC_DIR.resolve() not in file.parents or not file.is_file() or file.suffix not in TYPES:
        return no_store({"error": "not found"}, 404)
    return FileResponse(file, media_type=TYPES[file.suffix], headers={"Cache-Control": "no-cache"})
