"""Connect Instagram, Facebook and TikTok from the app: the platforms' login redirect lands on this
server, which swaps the code for tokens and queues them for the display.

The steps are the same as tools/auth/platforms.py (the command-line flow), using only the standard
library. Every call returns user-facing errors as OAuthError, and never puts a token in a message.
Set OAUTH_TEST_BASE to send every platform call to a fake server instead (tests only).
"""
from __future__ import annotations

import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request

META = "v26.0"  # keep in sync with tools/auth/common.py and firmware/master/src/sources/Source.h
DAY = 86400
PLATFORMS = ("instagram", "facebook", "tiktok")
TIKTOK_SCOPES = "user.info.basic,user.info.stats"

_REAL = {
    "ig_authorize": "https://www.instagram.com/oauth/authorize",
    "ig_token": "https://api.instagram.com/oauth/access_token",
    "ig_graph": "https://graph.instagram.com",
    "fb_dialog": f"https://www.facebook.com/{META}/dialog/oauth",
    "fb_graph": f"https://graph.facebook.com/{META}",
    "tt_authorize": "https://www.tiktok.com/v2/auth/authorize/",
    "tt_api": "https://open.tiktokapis.com/v2",
}


def url(name: str) -> str:
    base = os.environ.get("OAUTH_TEST_BASE")
    return f"{base.rstrip('/')}/{name}" if base else _REAL[name]


class OAuthError(Exception):
    pass


def _call(method: str, address: str, params: dict | None = None, form: dict | None = None,
          headers: dict | None = None) -> dict:
    if params:
        address += ("&" if "?" in address else "?") + urllib.parse.urlencode(params)
    data = urllib.parse.urlencode(form).encode() if form is not None else None
    req = urllib.request.Request(address, data=data, method=method, headers=headers or {})
    if data is not None:
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        with urllib.request.urlopen(req, timeout=15) as res:
            body = res.read(1_000_000)
    except urllib.error.HTTPError as e:
        body = e.read(100_000)
        try:
            doc = json.loads(body)
        except ValueError:
            raise OAuthError(f"the platform answered HTTP {e.code}") from None
        raise OAuthError(_error_text(doc) or f"the platform answered HTTP {e.code}") from None
    except (urllib.error.URLError, TimeoutError) as e:
        raise OAuthError(f"couldn't reach the platform ({getattr(e, 'reason', e)})") from None
    try:
        doc = json.loads(body)
    except ValueError:
        raise OAuthError("the platform sent something that isn't JSON") from None
    if not isinstance(doc, dict):
        raise OAuthError("the platform sent an unexpected answer")
    return doc


def _error_text(doc: dict) -> str:
    err = doc.get("error")
    if isinstance(err, dict):  # Meta: {"error": {"message": ...}}; TikTok: {"error": {"code", "message"}}
        msg = err.get("message") or err.get("code") or ""
        return str(msg)[:200]
    if isinstance(err, str):  # OAuth style: {"error": "...", "error_description": "..."}
        return f"{err} {doc.get('error_description', '')}".strip()[:200]
    if doc.get("error_message"):
        return str(doc["error_message"])[:200]
    return ""


# ---------------------------------------------------------------- the login page each platform shows

def authorize_url(platform: str, creds: dict, redirect: str, state: str) -> str:
    if platform == "instagram":
        return url("ig_authorize") + "?" + urllib.parse.urlencode({
            "client_id": creds["app_id"], "redirect_uri": redirect, "response_type": "code",
            "scope": "instagram_business_basic", "state": state})
    if platform == "facebook":
        return url("fb_dialog") + "?" + urllib.parse.urlencode({
            "client_id": creds["app_id"], "redirect_uri": redirect, "state": state, "response_type": "code",
            "scope": "pages_show_list,pages_read_engagement"})
    if platform == "tiktok":
        return url("tt_authorize") + "?" + urllib.parse.urlencode({
            "client_key": creds["app_id"], "scope": TIKTOK_SCOPES, "response_type": "code",
            "redirect_uri": redirect, "state": state})
    raise OAuthError("unknown platform")


# ---------------------------------------------------------------- swapping the code for tokens
# Each returns {"payload": <body for the display's POST /api/tokens>, "account": name, "followers": n}.
# Facebook with several Pages returns {"pages": [{"id", "name", "access_token"}]} instead, to choose from.

def exchange(platform: str, creds: dict, redirect: str, code: str) -> dict:
    now = int(time.time())
    app_id, secret = creds["app_id"], creds["app_secret"]
    if platform == "instagram":
        short = _call("POST", url("ig_token"), form={
            "client_id": app_id, "client_secret": secret, "grant_type": "authorization_code",
            "redirect_uri": redirect, "code": code})
        if "access_token" not in short:
            raise OAuthError(_error_text(short) or "Instagram didn't return a token")
        long_lived = _call("GET", url("ig_graph") + "/access_token", params={
            "grant_type": "ig_exchange_token", "client_secret": secret, "access_token": short["access_token"]})
        token, expires_in = long_lived["access_token"], int(long_lived.get("expires_in", 60 * DAY))
        me = _call("GET", url("ig_graph") + f"/{META}/me", params={"fields": "username,followers_count", "access_token": token})
        if "followers_count" not in me:
            raise OAuthError("the token works, but there's no follower count: is this a Professional (Business or Creator) account?")
        return {"payload": {"platform": "instagram", "access_token": token, "issued_at": now,
                            "expires_at": now + expires_in},
                "account": "@" + str(me.get("username", "")), "followers": int(me["followers_count"])}

    if platform == "facebook":
        user = _call("GET", url("fb_graph") + "/oauth/access_token", params={
            "client_id": app_id, "client_secret": secret, "redirect_uri": redirect, "code": code})
        long_user = _call("GET", url("fb_graph") + "/oauth/access_token", params={
            "grant_type": "fb_exchange_token", "client_id": app_id, "client_secret": secret,
            "fb_exchange_token": user["access_token"]})["access_token"]
        pages = _call("GET", url("fb_graph") + "/me/accounts",
                      params={"fields": "id,name,access_token", "access_token": long_user}).get("data", [])
        if not pages:
            raise OAuthError("no Pages found: your account must manage a Facebook Page, and you must select it when logging in")
        if len(pages) > 1:
            return {"pages": [{"id": p["id"], "name": p.get("name", p["id"]), "access_token": p["access_token"]} for p in pages]}
        return facebook_page(pages[0])

    if platform == "tiktok":
        tokens = _call("POST", url("tt_api") + "/oauth/token/", form={
            "client_key": app_id, "client_secret": secret, "code": code,
            "grant_type": "authorization_code", "redirect_uri": redirect})
        if "access_token" not in tokens:
            raise OAuthError(_error_text(tokens) or "TikTok didn't return a token")
        if "user.info.stats" not in tokens.get("scope", ""):
            raise OAuthError("TikTok didn't grant user.info.stats: add that scope to your app and approve it when logging in")
        info = _call("GET", url("tt_api") + "/user/info/", params={"fields": "display_name,follower_count"},
                     headers={"Authorization": f"Bearer {tokens['access_token']}"})
        user = info.get("data", {}).get("user", {})
        return {"payload": {
                    "platform": "tiktok", "access_token": tokens["access_token"],
                    "refresh_token": tokens["refresh_token"], "issued_at": now,
                    "expires_at": now + int(tokens.get("expires_in", DAY)),
                    "refresh_expires_at": now + int(tokens.get("refresh_expires_in", 365 * DAY)),
                    # the display refreshes the 24 h access token itself, which needs the app's key and secret
                    "client_key": app_id, "client_secret": secret},
                "account": str(user.get("display_name", "")), "followers": int(user.get("follower_count", 0))}
    raise OAuthError("unknown platform")


def facebook_page(page: dict) -> dict:
    """The chosen Page: check that its follower count can be read, then build the display's payload.
    A Page token derived from a long-lived user token doesn't expire."""
    info = _call("GET", url("fb_graph") + f"/{page['id']}",
                 params={"fields": "name,followers_count", "access_token": page["access_token"]})
    if "followers_count" not in info:
        raise OAuthError("the Page token works, but the follower count is missing: check the app's Page permissions")
    return {"payload": {"platform": "facebook", "access_token": page["access_token"], "page_id": page["id"],
                        "issued_at": int(time.time())},
            "account": str(info.get("name", page.get("name", ""))), "followers": int(info["followers_count"])}
