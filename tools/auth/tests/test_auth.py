"""Offline tests for the auth CLI.  Run from tools/:  python -m unittest discover -s auth/tests -t ."""

import hashlib
import http.server
import json
import pathlib
import socket
import tempfile
import threading
import unittest
import urllib.request

from auth import cli
from auth.common import AuthError, load_env, mask, parse_redirect, pkce_pair, wait_for_local_redirect, PKCE_CHARS
from auth.device import device_url, send_tokens, summarize
from auth.platforms import facebook_payload, instagram_payload, pick_page, tiktok_payload


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class PkceTest(unittest.TestCase):
    def test_hex_challenge(self):
        verifier, challenge = pkce_pair()
        self.assertEqual(64, len(verifier))
        self.assertTrue(set(verifier) <= set(PKCE_CHARS))
        self.assertEqual(hashlib.sha256(verifier.encode()).hexdigest(), challenge)  # hex, not base64url
        self.assertEqual(64, len(challenge))

    def test_fresh_each_time(self):
        self.assertNotEqual(pkce_pair()[0], pkce_pair()[0])

    def test_length_bounds(self):
        with self.assertRaises(ValueError):
            pkce_pair(42)
        self.assertEqual(128, len(pkce_pair(128)[0]))


class RedirectTest(unittest.TestCase):
    def test_code_and_instagram_suffix(self):
        # Instagram appends "#_": usually as a URL fragment, sometimes percent-encoded into the code
        self.assertEqual("AQB123", parse_redirect("https://localhost/?code=AQB123&state=s1#_", "s1"))
        self.assertEqual("AQB123", parse_redirect("https://localhost/?code=AQB123%23_&state=s1", "s1"))
        self.assertEqual("abc", parse_redirect("  http://127.0.0.1:8765/callback/?code=abc&state=xyz  ", "xyz"))

    def test_state_mismatch(self):
        with self.assertRaisesRegex(AuthError, "state mismatch"):
            parse_redirect("https://localhost/?code=abc&state=other", "mine")

    def test_error_param(self):
        with self.assertRaisesRegex(AuthError, "user denied"):
            parse_redirect("https://localhost/?error=access_denied&error_description=user+denied&state=s", "s")

    def test_missing_code(self):
        with self.assertRaises(AuthError):
            parse_redirect("https://localhost/?state=s", "s")
        with self.assertRaises(AuthError):
            parse_redirect("", "s")


class LocalCallbackTest(unittest.TestCase):
    def test_receives_redirect(self):
        port = free_port()
        result = {}
        t = threading.Thread(target=lambda: result.setdefault("url", wait_for_local_redirect(port, "/callback/", 10)))
        t.start()
        for _ in range(50):  # wait until the server is listening
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/callback/?code=c0de&state=st", timeout=2) as r:
                    self.assertIn(b"Login received", r.read())
                break
            except OSError:
                threading.Event().wait(0.05)
        t.join(5)
        self.assertEqual("c0de", parse_redirect(result["url"], "st"))


class EnvTest(unittest.TestCase):
    def test_parse_and_precedence(self):
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / ".env"
            path.write_text("# comment\nIG_APP_ID=123\nexport FB_APP_ID='456'\nTIKTOK_CLIENT_KEY=\"k=v\"\n\nBROKEN\n")
            env = {"IG_APP_ID": "from-environment"}
            load_env(path, env)
            self.assertEqual("from-environment", env["IG_APP_ID"])  # real env wins
            self.assertEqual("456", env["FB_APP_ID"])
            self.assertEqual("k=v", env["TIKTOK_CLIENT_KEY"])
            self.assertNotIn("BROKEN", env)

    def test_missing_file_is_fine(self):
        load_env(pathlib.Path("does-not-exist/.env"), {})


class PayloadTest(unittest.TestCase):
    def test_instagram(self):
        p = instagram_payload("IGtoken", 5183944, 1000)
        self.assertEqual({"platform": "instagram", "access_token": "IGtoken", "issued_at": 1000,
                          "expires_at": 1000 + 5183944}, p)

    def test_facebook(self):
        p = facebook_payload({"id": "42", "name": "Page", "access_token": "EAApage"}, 1000)
        self.assertEqual({"platform": "facebook", "access_token": "EAApage", "page_id": "42", "issued_at": 1000}, p)

    def test_tiktok(self):
        p = tiktok_payload({"access_token": "act.1", "refresh_token": "rft.1", "expires_in": 86400,
                            "refresh_expires_in": 31536000}, "key", "secret", 1000)
        self.assertEqual("rft.1", p["refresh_token"])
        self.assertEqual(1000 + 86400, p["expires_at"])
        self.assertEqual(1000 + 31536000, p["refresh_expires_at"])
        self.assertEqual(("key", "secret"), (p["client_key"], p["client_secret"]))

    def test_summary_masks_secrets(self):
        p = tiktok_payload({"access_token": "act.secretvalue1", "refresh_token": "rft.secretvalue2"}, "k", "csecret12345", 0)
        s = summarize(p)
        for secret in ("act.secretvalue1", "rft.secretvalue2", "csecret12345"):
            self.assertNotIn(secret, s)
        self.assertIn("****lue1", s)
        self.assertEqual("****", mask("short"))


class PickPageTest(unittest.TestCase):
    pages = [{"id": "1", "name": "A", "access_token": "t1"}, {"id": "2", "name": "B", "access_token": "t2"}]

    def test_single_and_explicit(self):
        self.assertEqual("1", pick_page(self.pages[:1], None)["id"])
        self.assertEqual("2", pick_page(self.pages, "2")["id"])
        with self.assertRaises(AuthError):
            pick_page(self.pages, "3")
        with self.assertRaises(AuthError):
            pick_page([], None)

    def test_prompt(self):
        self.assertEqual("2", pick_page(self.pages, None, choose=lambda _: "2")["id"])
        with self.assertRaises(AuthError):
            pick_page(self.pages, None, choose=lambda _: "9")


class DeviceTest(unittest.TestCase):
    def test_url(self):
        self.assertEqual("http://splitflap.local/api/tokens", device_url("splitflap.local"))
        self.assertEqual("http://192.168.1.50/api/tokens", device_url("http://192.168.1.50/"))

    def test_post_to_fake_device(self):
        received = {}

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):  # noqa: N802
                received["path"] = self.path
                received["key"] = self.headers.get("X-Admin-Key")
                received["body"] = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                ok = received["key"] == "right-key"
                self.send_response(200 if ok else 401)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(b'{"ok":true}' if ok else b'{"error":"missing or wrong X-Admin-Key"}')

            def log_message(self, *args):
                pass

        server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        host = f"127.0.0.1:{server.server_port}"
        try:
            payload = instagram_payload("IGtoken", 100, 1)
            send_tokens(host, payload, "right-key")
            self.assertEqual("/api/tokens", received["path"])
            self.assertEqual(payload, received["body"])
            with self.assertRaisesRegex(AuthError, "401.*X-Admin-Key"):
                send_tokens(host, payload, "wrong-key")
        finally:
            server.shutdown()
            server.server_close()

    def test_unreachable(self):
        with self.assertRaisesRegex(AuthError, "can't reach the device"):
            send_tokens(f"127.0.0.1:{free_port()}", {}, "k", timeout=2)


class CliTest(unittest.TestCase):
    def test_help_and_bad_platform(self):
        with self.assertRaises(SystemExit) as e:
            cli.build_parser().parse_args(["--help"])
        self.assertEqual(0, e.exception.code)
        with self.assertRaises(SystemExit):
            cli.build_parser().parse_args(["myspace"])


if __name__ == "__main__":
    unittest.main()
