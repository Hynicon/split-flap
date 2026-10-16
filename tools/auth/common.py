"""Shared helpers: .env loading, HTTP, OAuth redirect parsing, PKCE, masking. Standard library only."""

from __future__ import annotations

import hashlib
import http.server
import json
import os
import pathlib
import secrets
import string
import threading
import urllib.error
import urllib.parse
import urllib.request

META_GRAPH_VERSION = "v26.0"  # keep in sync with firmware/master/src/sources/Source.h
ENV_FILE = pathlib.Path(__file__).resolve().parent / ".env"


class AuthError(Exception):
    """A user-facing failure (bad config, denied login, API error)."""


# ---------- configuration ----------

def load_env(path: pathlib.Path = ENV_FILE, environ: dict | None = None) -> None:
    """Load KEY=VALUE lines from a git-ignored .env file. Real environment variables win."""
    environ = os.environ if environ is None else environ
    if not path.exists():
        return
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key, value = key.strip(), value.strip()
        if key.startswith("export "):
            key = key[len("export "):].strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
            value = value[1:-1]
        environ.setdefault(key, value)


def require_env(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if not value:
        raise AuthError(f"{name} is not set. Put it in tools/auth/.env (see .env.example) or the environment.")
    return value


def mask(secret: str) -> str:
    if not secret:
        return ""
    return "****" if len(secret) <= 8 else "****" + secret[-4:]


# ---------- HTTP ----------

def _read_json(req: urllib.request.Request, timeout: float = 20) -> dict:
    try:
        with urllib.request.urlopen(req, timeout=timeout) as res:
            body = res.read()
    except urllib.error.HTTPError as e:
        with e:
            body = e.read()
        try:
            data = json.loads(body or b"{}")
        except ValueError:
            raise AuthError(f"HTTP {e.code} from {req.host}") from None
        raise AuthError(f"HTTP {e.code} from {req.host}: {describe_api_error(data)}") from None
    except urllib.error.URLError as e:
        raise AuthError(f"can't reach {req.host}: {e.reason}") from None
    try:
        return json.loads(body or b"{}")
    except ValueError:
        raise AuthError(f"non-JSON response from {req.host}") from None


def describe_api_error(data: dict) -> str:
    err = data.get("error")
    if isinstance(err, dict):  # Meta: {"error":{"message","code"}}, TikTok: {"error":{"code","message"}}
        return f"{err.get('code')}: {err.get('message')}"
    if isinstance(err, str):  # OAuth style: {"error","error_description"}
        return f"{err}: {data.get('error_description') or data.get('error_message') or ''}".strip()
    if "error_message" in data:  # api.instagram.com
        return f"{data.get('error_type')}: {data['error_message']}"
    return json.dumps(data)[:200]


def get_json(url: str, params: dict | None = None, headers: dict | None = None) -> dict:
    if params:
        url += ("&" if "?" in url else "?") + urllib.parse.urlencode(params)
    return _read_json(urllib.request.Request(url, headers=headers or {}))


def post_form(url: str, fields: dict) -> dict:
    data = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    return _read_json(req)


# ---------- OAuth helpers ----------

def new_state() -> str:
    return secrets.token_urlsafe(24)


PKCE_CHARS = string.ascii_letters + string.digits + "-._~"


def pkce_pair(length: int = 64) -> tuple[str, str]:
    """TikTok desktop PKCE: verifier from the unreserved set, challenge = HEX sha256 (not base64url)."""
    if not 43 <= length <= 128:
        raise ValueError("PKCE verifier must be 43..128 characters")
    verifier = "".join(secrets.choice(PKCE_CHARS) for _ in range(length))
    return verifier, hashlib.sha256(verifier.encode("ascii")).hexdigest()


def parse_redirect(url: str, expected_state: str) -> str:
    """Return the auth code from a pasted redirect URL; checks state and error parameters."""
    url = url.strip()
    if not url:
        raise AuthError("no URL pasted")
    parsed = urllib.parse.urlparse(url)
    query = urllib.parse.parse_qs(parsed.query)
    if "error" in query:
        reason = query.get("error_description", query.get("error_reason", query["error"]))[0]
        raise AuthError(f"login was not completed: {reason}")
    state = query.get("state", [""])[0]
    if not secrets.compare_digest(state, expected_state):
        raise AuthError("state mismatch: paste the URL from the login you just started")
    code = query.get("code", [""])[0]
    if code.endswith("#_"):  # Instagram appends "#_" to the code
        code = code[:-2]
    if not code:
        raise AuthError("no ?code= in that URL")
    return code


def wait_for_local_redirect(port: int, path: str, timeout: float = 300) -> str:
    """Serve http://127.0.0.1:<port><path> once and return the full redirect URL it received."""
    result: dict = {}
    done = threading.Event()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802 (http.server API)
            if urllib.parse.urlparse(self.path).path.rstrip("/") != path.rstrip("/"):
                self.send_error(404)
                return
            result["url"] = f"http://127.0.0.1:{port}{self.path}"
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"<p>Login received. You can close this tab and return to the terminal.</p>")
            done.set()

        def log_message(self, *args):  # keep codes out of the terminal
            pass

    server = http.server.HTTPServer(("127.0.0.1", port), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        if not done.wait(timeout):
            raise AuthError(f"no login callback within {int(timeout)} s")
        return result["url"]
    finally:
        server.shutdown()
        server.server_close()
