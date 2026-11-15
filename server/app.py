"""Relay server for the split-flap display.

The display sits behind a network that nothing can connect in to (a phone hotspot), so it calls
out to this server. The server:

- serves the phone app (the same files the ESP32 serves, from firmware/master/data);
- puts a login in front of everything;
- queues the changes made in the app until the display collects them (POST /api/device/sync,
  which the display holds open so a change arrives within about a second);
- remembers the status and settings the display last reported, so the app has something to show.

The display stays the source of truth: the server never decides what is shown.

On first visit it shows a setup page that creates the sign-in account and the display's device
token; both are kept (hashed) in DATA_DIR/auth.json. The first visitor sets it up, so open it
right after deploying. To start over, delete auth.json and restart.

Environment (all optional): APP_USERNAME and APP_PASSWORD fix the account instead of the setup
page, DEVICE_TOKEN fixes the display's token, DATA_DIR, STATIC_DIR, SECRET_KEY (generated and kept
in DATA_DIR otherwise), TRUST_PROXY=1 when the server is only reachable through a reverse proxy
(the Docker image sets it).
"""

from __future__ import annotations

import asyncio
from collections import OrderedDict, deque
import ipaddress
import copy
import hashlib
import hmac
import html
import json
import re
import os
import secrets
import time
import urllib.parse
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, RedirectResponse, Response

ENV_USERNAME = os.environ.get("APP_USERNAME") or "admin"
ENV_PASSWORD = os.environ.get("APP_PASSWORD", "")
ENV_DEVICE_TOKEN = os.environ.get("DEVICE_TOKEN", "")
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

MIN_PASSWORD = 12
if ENV_PASSWORD and len(ENV_PASSWORD) < 8:
    raise SystemExit("APP_PASSWORD must be at least 8 characters, or leave it unset to use the setup page.")
if ENV_DEVICE_TOKEN and len(ENV_DEVICE_TOKEN) < 16:
    raise SystemExit("DEVICE_TOKEN must be at least 16 characters, or leave it unset to make one on the setup page.")

DATA_DIR.mkdir(parents=True, exist_ok=True)
STATE_FILE = DATA_DIR / "state.json"
KEY_FILE = DATA_DIR / "secret.key"
AUTH_FILE = DATA_DIR / "auth.json"


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


def write_private(path: Path, text: str) -> None:
    tmp = path.with_suffix(".tmp")
    tmp.write_text(text, encoding="utf-8")
    try:
        tmp.chmod(0o600)
    except OSError:
        pass
    tmp.replace(path)


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    digest = hashlib.scrypt(password.encode(), salt=salt, n=2**14, r=8, p=1, dklen=32)
    return f"scrypt$16384$8$1${salt.hex()}${digest.hex()}"


def check_password(password: str, stored: str) -> bool:
    try:
        _, n, r, p, salt, digest = stored.split("$")
        got = hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt), n=int(n), r=int(r), p=int(p), dklen=32)
        return hmac.compare_digest(got.hex(), digest)
    except (ValueError, TypeError):
        return False


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


TZ_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_+\-/]{0,63}$")
USERNAME_RE = re.compile(r"^\S{1,40}$")


def clean_name(value: object) -> str | None:
    """A display name: printable, at most 40 characters. None if it isn't text."""
    if not isinstance(value, str):
        return None
    return "".join(ch for ch in value if ch.isprintable()).strip()[:40]


def same(a: str, b: str) -> bool:
    return hmac.compare_digest(a.encode(), b.encode())


class Account:
    """The sign-in account, the device token and the server's own settings (DATA_DIR/auth.json).
    APP_PASSWORD and DEVICE_TOKEN, when set, take the place of the stored ones."""

    def __init__(self) -> None:
        self.lock = asyncio.Lock()
        try:
            self.data: dict = json.loads(AUTH_FILE.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            self.data = {}

    def save(self) -> None:
        write_private(AUTH_FILE, json.dumps(self.data))

    @property
    def password_from_env(self) -> bool:
        return bool(ENV_PASSWORD)

    @property
    def token_from_env(self) -> bool:
        return bool(ENV_DEVICE_TOKEN)

    @property
    def configured(self) -> bool:
        return self.password_from_env or bool(self.data.get("password"))

    @property
    def username(self) -> str:
        return ENV_USERNAME if self.password_from_env else self.data.get("username", "")

    @property
    def session_epoch(self) -> str:
        """Part of every session's signature. It changes with the password, never repeats (random),
        and differs after a reset, so old sessions stop working in all those cases."""
        if self.password_from_env:
            return hashlib.sha256(f"env:{ENV_USERNAME}:{ENV_PASSWORD}".encode()).hexdigest()
        return self.data.get("session_epoch", "")

    @property
    def name(self) -> str:
        return self.data.get("display_name", "") or "Split-Flap"

    @property
    def time_zone(self) -> str:
        return self.data.get("time_zone", "")

    @property
    def has_device_token(self) -> bool:
        return self.token_from_env or bool(self.data.get("device_token"))

    def password_ok(self, password: str) -> bool:
        if self.password_from_env:
            return same(password, ENV_PASSWORD)
        stored = self.data.get("password")
        return bool(stored) and check_password(password, stored)

    def login_ok(self, username: str, password: str) -> bool:
        name_ok = same(username.strip().lower(), self.username.lower())
        return self.password_ok(password) and name_ok  # the password is always checked: same time either way

    def device_token_ok(self, token: str) -> bool:
        if self.token_from_env:
            return same(token, ENV_DEVICE_TOKEN)
        stored = self.data.get("device_token")
        return bool(stored) and same(token_hash(token), stored)

    def new_device_token(self) -> str:
        token = secrets.token_hex(24)
        self.data["device_token"] = token_hash(token)
        self.save()
        return token

    def set_login(self, username: str, password: str) -> None:
        self.data["username"] = username
        self.data["password"] = hash_password(password)
        self.data["session_epoch"] = secrets.token_hex(16)  # signs out every other session
        self.save()


app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
state = State()
account = Account()

# ---------------------------------------------------------------- login


def sign(expires: int) -> str:
    mac = hmac.new(SECRET, f"{expires}.{account.session_epoch}".encode(), hashlib.sha256).hexdigest()
    return f"{expires}.{mac}"


def valid_session(cookie: str | None) -> bool:
    """A session is valid until it expires, the password changes or the account is reset."""
    if not account.session_epoch:
        return False
    try:
        expires = int((cookie or "").split(".", 1)[0])
        return expires > time.time() and hmac.compare_digest(sign(expires), cookie or "")
    except ValueError:
        return False


def start_session(res: Response, request: Request) -> None:
    res.set_cookie("session", sign(int(time.time()) + SESSION_DAYS * 86400), max_age=SESSION_DAYS * 86400,
                   httponly=True, secure=is_https(request), samesite="lax")


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
    if account.password_ok(key):
        good_password(ip)
        return True
    wrong_password(ip)
    await asyncio.sleep(1)
    return False


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>__TITLE__</title><link rel="stylesheet" href="/style.css"></head>
<body class="login"><main>__BODY__</main>__SCRIPT__</body></html>"""


def page(title: str, body: str, code: int = 200, script: str = "") -> HTMLResponse:
    doc = PAGE.replace("__TITLE__", html.escape(title)).replace("__SCRIPT__", script).replace("__BODY__", body)
    return HTMLResponse(doc, status_code=code, headers={"Cache-Control": "no-store"})


def login_page_html(error: str = "", code: int = 200, username: str = "") -> HTMLResponse:
    return page(f"{account.name}: sign in", f"""<form method="post" action="/login" class="card">
<h1>{html.escape(account.name)}</h1>
<label>Username <input name="username" autocomplete="username" autocapitalize="none" value="{html.escape(username)}" required></label>
<label>Password <input name="password" type="password" autocomplete="current-password" required></label>
<p class="error" role="alert">{html.escape(error)}</p>
<button type="submit">Sign in</button>
</form>""", code)


# Fills in the browser's time zone, and checks that the two passwords match before sending.
SETUP_SCRIPT = """<script>
const tz = document.querySelector("[name=time_zone]");
if (!tz.value) try { tz.value = Intl.DateTimeFormat().resolvedOptions().timeZone || ""; } catch (e) {}
document.querySelector("form").addEventListener("submit", (ev) => {
  const f = ev.target.elements;
  if (f.password.value !== f.password2.value) {
    ev.preventDefault();
    document.querySelector(".error").textContent = "The two passwords don't match.";
  }
});
</script>"""


def setup_page_html(error: str = "", code: int = 200, form: dict | None = None) -> HTMLResponse:
    f = {k: html.escape(v) for k, v in (form or {}).items()}
    return page("Split-Flap: set up", f"""<form method="post" action="/setup" class="card">
<h1>Set up Split-Flap</h1>
<p class="hint">Create the account you'll sign in with. This page only appears once: whoever fills it in first owns this server.</p>
<label>Name shown in the app <input name="display_name" maxlength="40" placeholder="Split-Flap" value="{f.get('display_name', '')}"></label>
<label>Username <input name="username" autocomplete="username" autocapitalize="none" maxlength="40" required value="{f.get('username', '')}"></label>
<label>Password <input name="password" type="password" autocomplete="new-password" minlength="{MIN_PASSWORD}" required></label>
<label>Password again <input name="password2" type="password" autocomplete="new-password" minlength="{MIN_PASSWORD}" required></label>
<p class="hint">At least {MIN_PASSWORD} characters. This server faces the internet, so a long random password is best.</p>
<label>Time zone <input name="time_zone" autocomplete="off" placeholder="Europe/Lisbon" value="{f.get('time_zone', '')}"></label>
<p class="error" role="alert">{html.escape(error)}</p>
<button type="submit">Create account</button>
</form>""", code, SETUP_SCRIPT)


def setup_done_html(token: str | None) -> HTMLResponse:
    if token:
        token_part = f"""<p>This is the display's <b>device token</b>. It is shown only now, so copy it before you leave this page:</p>
<div class="inline"><input class="token" readonly value="{html.escape(token)}" onclick="this.select()" aria-label="Device token"><button type="button" class="quiet" onclick="navigator.clipboard.writeText(this.previousElementSibling.value).then(() => this.textContent = 'Copied', () => this.previousElementSibling.select())">Copy</button></div>
<p class="hint">On the display's own page, open <b>Settings → App server</b> (or, in its setup portal, the App server fields) and enter this server's address and this token. You can make a new token later under <b>Settings → Server</b>.</p>"""
    else:
        token_part = '<p class="hint">The device token is set by the DEVICE_TOKEN variable.</p>'
    return page("Split-Flap: ready", f"""<div class="card">
<h1>All set</h1>
{token_part}
<a class="button" href="/">Open the app</a>
</div>""")


PUBLIC_PATHS = {"/login", "/setup", "/style.css", "/healthz", "/api/device/sync"}


def form_fields(raw: bytes) -> dict[str, str]:
    q = urllib.parse.parse_qs(raw[:8192].decode("utf-8", "replace"), keep_blank_values=True)
    return {k: v[0] for k, v in q.items()}


def from_elsewhere(request: Request) -> bool:
    """A form posted by another website (browsers say so in Sec-Fetch-Site)."""
    return request.headers.get("sec-fetch-site", "same-origin") not in ("same-origin", "none")


@app.middleware("http")
async def require_login(request: Request, call_next):
    path = request.url.path
    if not account.configured and path not in PUBLIC_PATHS:
        if path.startswith("/api/"):
            return JSONResponse({"error": "This server hasn't been set up yet. Open it in a browser."}, status_code=503)
        return RedirectResponse("/setup", status_code=303)
    if path in PUBLIC_PATHS or await logged_in(request):
        return await call_next(request)
    if path.startswith("/api/"):
        return JSONResponse({"error": "not signed in"}, status_code=401)
    return RedirectResponse("/login", status_code=303)


@app.get("/setup")
async def setup_page():
    if account.configured:
        return RedirectResponse("/login", status_code=303)
    return setup_page_html()


@app.post("/setup")
async def setup(request: Request):
    form = form_fields(await request.body())
    async with account.lock:  # two visitors at once: the first one wins
        if account.configured:
            return RedirectResponse("/login", status_code=303)
        if from_elsewhere(request):
            return setup_page_html("Open this page directly to set up the server.", 403)
        username = form.get("username", "").strip()
        password = form.get("password", "")
        name = clean_name(form.get("display_name", "")) or ""
        tz = form.get("time_zone", "").strip()
        keep = {"username": username, "display_name": name, "time_zone": tz}
        if not USERNAME_RE.match(username):
            return setup_page_html("Choose a username of up to 40 characters, without spaces.", 400, keep)
        if len(password) < MIN_PASSWORD:
            return setup_page_html(f"The password needs at least {MIN_PASSWORD} characters.", 400, keep)
        if password != form.get("password2", ""):
            return setup_page_html("The two passwords don't match.", 400, keep)
        if tz and not TZ_RE.match(tz):
            return setup_page_html("That doesn't look like a time zone name, such as Europe/Lisbon.", 400, keep)
        account.data.update({"display_name": name, "time_zone": tz})
        account.set_login(username, password)
        token = None if account.token_from_env else account.new_device_token()
        print(f"Setup finished: account '{username}' created.", flush=True)
    res = setup_done_html(token)
    start_session(res, request)
    return res


@app.get("/login")
async def login_page(request: Request):
    if not account.configured:
        return RedirectResponse("/setup", status_code=303)
    if await logged_in(request):
        return RedirectResponse("/", status_code=303)
    return login_page_html()


LOCKED_MSG = "Too many wrong passwords from your network. Wait five minutes and try again."


@app.post("/login")
async def login(request: Request):
    if not account.configured:
        return RedirectResponse("/setup", status_code=303)
    ip = client_ip(request)
    if locked_out(ip):
        return login_page_html(LOCKED_MSG, 429)
    form = form_fields(await request.body())
    username = form.get("username", "")
    if locked_out(ip):  # again: other guesses may have landed while the body was read
        return login_page_html(LOCKED_MSG, 429, username)
    if not account.login_ok(username, form.get("password", "")):
        wrong_password(ip)
        await asyncio.sleep(1)
        return login_page_html("Wrong username or password.", 401, username)
    good_password(ip)
    res = RedirectResponse("/", status_code=303)
    start_session(res, request)
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
    if not auth.startswith("Bearer ") or not account.device_token_ok(auth[7:]):
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
    status["server"] = {"name": account.name, "time_zone": account.time_zone}
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


# ---------------------------------------------------------------- the server's own settings


def server_info() -> dict:
    return {
        "username": account.username,
        "display_name": account.data.get("display_name", ""),
        "time_zone": account.time_zone,
        "password_from_env": account.password_from_env,
        "device_token_from_env": account.token_from_env,
        "device_token_set": account.has_device_token,
    }


async def current_password_ok(request: Request, body: dict) -> bool:
    """Changing the account or the device token needs the current password, counted like a sign-in."""
    ip = client_ip(request)
    if locked_out(ip):
        return False
    if account.password_ok(str(body.get("current_password", ""))):
        return True
    wrong_password(ip)
    await asyncio.sleep(1)
    return False


@app.get("/api/server")
async def api_server():
    return no_store(server_info())


@app.post("/api/server")
async def api_server_post(request: Request):
    body = await read_json(request)
    if body is None:
        return no_store({"error": "expected a JSON object"}, 400)
    if "display_name" in body and clean_name(body["display_name"]) is None:
        return no_store({"error": "display_name must be text"}, 400)
    if "time_zone" in body:
        tz = body["time_zone"]
        if not isinstance(tz, str) or (tz.strip() and not TZ_RE.match(tz.strip())):
            return no_store({"error": "That doesn't look like a time zone name, such as Europe/Lisbon."}, 400)
    changes_login = "new_password" in body or "username" in body
    password_changed = False
    async with account.lock:
        if changes_login:
            if account.password_from_env:
                return no_store({"error": "The account is set by the APP_PASSWORD variable."}, 400)
            username = str(body.get("username", account.username)).strip()
            new_password = body.get("new_password")
            if not USERNAME_RE.match(username):
                return no_store({"error": "Choose a username of up to 40 characters, without spaces."}, 400)
            if new_password is not None and (not isinstance(new_password, str) or len(new_password) < MIN_PASSWORD):
                return no_store({"error": f"The new password needs at least {MIN_PASSWORD} characters."}, 400)
            if not await current_password_ok(request, body):
                return no_store({"error": "The current password is wrong."}, 403)
            if new_password is None:
                account.data["username"] = username
            else:
                account.set_login(username, new_password)
                password_changed = True
        if "display_name" in body:
            account.data["display_name"] = clean_name(body["display_name"]) or ""
        if "time_zone" in body:
            account.data["time_zone"] = body["time_zone"].strip()
        account.save()
    res = no_store({**server_info(), "signed_out_others": password_changed})
    if password_changed:
        start_session(res, request)  # this phone stays signed in under the new password
    return res


@app.post("/api/server/device-token")
async def api_device_token(request: Request):
    body = await read_json(request)
    if body is None:
        return no_store({"error": "expected a JSON object"}, 400)
    if account.token_from_env:
        return no_store({"error": "The device token is set by the DEVICE_TOKEN variable."}, 400)
    async with account.lock:
        if not await current_password_ok(request, body):
            return no_store({"error": "The current password is wrong."}, 403)
        token = account.new_device_token()
    return no_store({"device_token": token})


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
