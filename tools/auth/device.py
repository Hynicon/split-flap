"""Send tokens to the split-flap device (POST /api/tokens)."""

from __future__ import annotations

import getpass
import json
import os
import urllib.error
import urllib.request

from .common import AuthError, mask

SECRET_FIELDS = ("access_token", "refresh_token", "client_secret")


def summarize(payload: dict) -> str:
    shown = {k: (mask(v) if k in SECRET_FIELDS else v) for k, v in payload.items()}
    return json.dumps(shown, indent=2)


def device_url(host: str) -> str:
    host = host.strip().rstrip("/")
    if not host.startswith(("http://", "https://")):
        host = "http://" + host
    return host + "/api/tokens"


def admin_key() -> str:
    key = os.environ.get("SPLITFLAP_ADMIN_KEY", "")
    if not key:
        key = getpass.getpass("Device admin key (set in the SplitFlap-Setup portal): ")
    if not key:
        raise AuthError("an admin key is required to send tokens to the device")
    return key


def send_tokens(host: str, payload: dict, key: str, timeout: float = 15) -> None:
    req = urllib.request.Request(
        device_url(host),
        data=json.dumps(payload).encode(),
        method="POST",
        headers={"Content-Type": "application/json", "X-Admin-Key": key},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as res:
            res.read()
    except urllib.error.HTTPError as e:
        with e:
            raw = e.read()
        try:
            detail = json.loads(raw or b"{}").get("error", "")
        except ValueError:
            detail = ""
        raise AuthError(f"device rejected the tokens (HTTP {e.code}) {detail}".strip()) from None
    except urllib.error.URLError as e:
        raise AuthError(f"can't reach the device at {host}: {e.reason}. Try --device <ip-address>.") from None
