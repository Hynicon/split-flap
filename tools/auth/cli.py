from __future__ import annotations

import argparse
import os
import sys

from .common import AuthError, load_env
from .device import admin_key, send_tokens, summarize
from .platforms import FLOWS


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="python -m auth",
        description="Log in to a platform once and send long-lived tokens to the split-flap display. "
                    "App credentials come from tools/auth/.env or environment variables (see .env.example).")
    p.add_argument("platform", choices=sorted(FLOWS))
    p.add_argument("--device", default=os.environ.get("SPLITFLAP_HOST", "splitflap.local"),
                   help="device host or IP (default: $SPLITFLAP_HOST or splitflap.local)")
    p.add_argument("--dry-run", action="store_true", help="log in and verify, but don't send anything to the device")
    p.add_argument("--redirect-uri", help="override the redirect URI registered in your app")
    p.add_argument("--token", help="instagram/facebook: skip the browser login and use this token "
                                   "(IG long-lived token from the app dashboard, or FB user token from Graph API Explorer)")
    p.add_argument("--page-id", help="facebook: Page to use when the account manages several")
    p.add_argument("--port", type=int, default=8765, help="tiktok: local callback port (default 8765)")
    p.add_argument("--paste", action="store_true", help="tiktok: paste the redirect URL instead of the local callback")
    return p


def main(argv: list[str] | None = None) -> int:
    load_env()
    args = build_parser().parse_args(argv)
    try:
        payload = FLOWS[args.platform](args)
        print("\nTokens (masked):\n" + summarize(payload))
        if args.dry_run:
            print("\n--dry-run: nothing sent to the device.")
            return 0
        send_tokens(args.device, payload, admin_key())
        print(f"\nSent to {args.device}. The display will fetch {args.platform} within a few seconds.")
        return 0
    except AuthError as e:
        print(f"\nerror: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\ncancelled", file=sys.stderr)
        return 130
