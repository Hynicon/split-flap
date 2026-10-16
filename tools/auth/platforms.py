"""The three login flows. Each returns the JSON payload for the device's POST /api/tokens."""

from __future__ import annotations

import time
import urllib.parse
import webbrowser

from .common import (META_GRAPH_VERSION, AuthError, get_json, new_state, parse_redirect, pkce_pair,
                     post_form, require_env, wait_for_local_redirect)

DAY = 86400


def _open(url: str) -> None:
    print("\nOpen this URL and log in (it should open automatically):\n\n  " + url + "\n")
    webbrowser.open(url)


def _paste_redirect(state: str) -> str:
    print("After approving, your browser is sent to the redirect URI. The page itself may fail to load,")
    print("that's fine: copy the FULL address from the address bar and paste it here.")
    return parse_redirect(input("Redirected URL: "), state)


# ---------------- Instagram (Instagram API with Instagram Login) ----------------

def instagram_payload(access_token: str, expires_in: int, now: int) -> dict:
    return {"platform": "instagram", "access_token": access_token, "issued_at": now,
            "expires_at": now + (expires_in or 60 * DAY)}


def instagram(args) -> dict:
    now = int(time.time())
    if args.token:
        token, expires_in = args.token, 60 * DAY  # a long-lived token generated in the app dashboard
    else:
        app_id = require_env("IG_APP_ID")
        app_secret = require_env("IG_APP_SECRET")
        redirect = args.redirect_uri or "https://localhost/"
        state = new_state()
        _open("https://www.instagram.com/oauth/authorize?" + urllib.parse.urlencode({
            "client_id": app_id, "redirect_uri": redirect, "response_type": "code",
            "scope": "instagram_business_basic", "state": state}))
        code = _paste_redirect(state)
        short = post_form("https://api.instagram.com/oauth/access_token", {
            "client_id": app_id, "client_secret": app_secret, "grant_type": "authorization_code",
            "redirect_uri": redirect, "code": code})
        long_lived = get_json("https://graph.instagram.com/access_token", {
            "grant_type": "ig_exchange_token", "client_secret": app_secret, "access_token": short["access_token"]})
        token, expires_in = long_lived["access_token"], int(long_lived.get("expires_in", 60 * DAY))

    me = get_json(f"https://graph.instagram.com/{META_GRAPH_VERSION}/me",
                  {"fields": "username,followers_count", "access_token": token})
    if "followers_count" not in me:
        raise AuthError("token works but no followers_count: is this a Professional (Business/Creator) account?")
    print(f"Instagram @{me.get('username')}: {me['followers_count']:,} followers")
    return instagram_payload(token, expires_in, now)


# ---------------- Facebook Page ----------------

def pick_page(pages: list[dict], page_id: str | None, choose=input) -> dict:
    if not pages:
        raise AuthError("no Pages found: the account must be an admin of a Facebook Page, and the Page must be "
                        "selected during login")
    if page_id:
        for p in pages:
            if p["id"] == page_id:
                return p
        raise AuthError(f"page {page_id} not in this account's Pages")
    if len(pages) == 1:
        return pages[0]
    for i, p in enumerate(pages, 1):
        print(f"  {i}. {p['name']} ({p['id']})")
    choice = choose("Which Page? ").strip()
    if not choice.isdigit() or not 1 <= int(choice) <= len(pages):
        raise AuthError("invalid choice")
    return pages[int(choice) - 1]


def facebook_payload(page: dict, now: int) -> dict:
    # A Page token derived from a long-lived user token does not expire (expires_at 0).
    return {"platform": "facebook", "access_token": page["access_token"], "page_id": page["id"], "issued_at": now}


def facebook(args) -> dict:
    graph = f"https://graph.facebook.com/{META_GRAPH_VERSION}"
    app_id = require_env("FB_APP_ID")
    app_secret = require_env("FB_APP_SECRET")
    if args.token:
        user_token = args.token  # e.g. from the Graph API Explorer
    else:
        redirect = args.redirect_uri or "https://localhost/"
        state = new_state()
        _open(f"https://www.facebook.com/{META_GRAPH_VERSION}/dialog/oauth?" + urllib.parse.urlencode({
            "client_id": app_id, "redirect_uri": redirect, "state": state, "response_type": "code",
            "scope": "pages_show_list,pages_read_engagement"}))
        code = _paste_redirect(state)
        user_token = get_json(f"{graph}/oauth/access_token", {
            "client_id": app_id, "client_secret": app_secret, "redirect_uri": redirect, "code": code})["access_token"]

    long_user = get_json(f"{graph}/oauth/access_token", {
        "grant_type": "fb_exchange_token", "client_id": app_id, "client_secret": app_secret,
        "fb_exchange_token": user_token})["access_token"]
    pages = get_json(f"{graph}/me/accounts", {"fields": "id,name,access_token", "access_token": long_user})
    page = pick_page(pages.get("data", []), args.page_id)
    info = get_json(f"{graph}/{page['id']}", {"fields": "name,followers_count", "access_token": page["access_token"]})
    if "followers_count" not in info:
        raise AuthError("Page token works but followers_count is missing: check the app's Page permissions")
    print(f"Facebook Page {info.get('name')}: {info['followers_count']:,} followers")
    return facebook_payload(page, int(time.time()))


# ---------------- TikTok (Login Kit for Desktop: localhost redirect + PKCE) ----------------

TIKTOK_SCOPES = "user.info.basic,user.info.stats"


def tiktok_payload(tokens: dict, client_key: str, client_secret: str, now: int) -> dict:
    return {
        "platform": "tiktok",
        "access_token": tokens["access_token"],
        "refresh_token": tokens["refresh_token"],
        "issued_at": now,
        "expires_at": now + int(tokens.get("expires_in", DAY)),
        "refresh_expires_at": now + int(tokens.get("refresh_expires_in", 365 * DAY)),
        # the device refreshes the 24 h access token itself, which needs the app credentials
        "client_key": client_key,
        "client_secret": client_secret,
    }


def tiktok(args) -> dict:
    client_key = require_env("TIKTOK_CLIENT_KEY")
    client_secret = require_env("TIKTOK_CLIENT_SECRET")
    redirect = args.redirect_uri or f"http://127.0.0.1:{args.port}/callback/"
    verifier, challenge = pkce_pair()
    state = new_state()
    _open("https://www.tiktok.com/v2/auth/authorize/?" + urllib.parse.urlencode({
        "client_key": client_key, "scope": TIKTOK_SCOPES, "response_type": "code", "redirect_uri": redirect,
        "state": state, "code_challenge": challenge, "code_challenge_method": "S256"}))
    if args.paste:
        code = _paste_redirect(state)
    else:
        print(f"Waiting for the login callback on {redirect} ...")
        path = urllib.parse.urlparse(redirect).path or "/"
        code = parse_redirect(wait_for_local_redirect(args.port, path), state)

    tokens = post_form("https://open.tiktokapis.com/v2/oauth/token/", {
        "client_key": client_key, "client_secret": client_secret, "code": code,
        "grant_type": "authorization_code", "redirect_uri": redirect, "code_verifier": verifier})
    if "access_token" not in tokens:
        raise AuthError(f"TikTok token exchange failed: {tokens.get('error')} {tokens.get('error_description', '')}")
    if "user.info.stats" not in tokens.get("scope", ""):
        raise AuthError("TikTok did not grant user.info.stats: add the scope to the app and approve it at login")

    info = get_json("https://open.tiktokapis.com/v2/user/info/", {"fields": "display_name,follower_count"},
                    headers={"Authorization": f"Bearer {tokens['access_token']}"})
    user = info.get("data", {}).get("user", {})
    print(f"TikTok {user.get('display_name')}: {user.get('follower_count', 0):,} followers")
    return tiktok_payload(tokens, client_key, client_secret, int(time.time()))


FLOWS = {"instagram": instagram, "facebook": facebook, "tiktok": tiktok}
