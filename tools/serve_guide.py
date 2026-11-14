"""Open the build guide (hardware/guide/index.html) in the browser.

The guide loads its 3D models with fetch(), which browsers block for file://
pages, so this serves the folder on localhost and opens it.

    python tools/serve_guide.py              # http://127.0.0.1:8770/
    python tools/serve_guide.py 9000         # another port
    python tools/serve_guide.py --lan        # also reachable from phones etc. on the same network

With --lan the server listens on every network interface and prints the
address to open on the other device. Windows asks once whether to allow
Python through the firewall: allow it on private networks.

Ctrl+C stops it. Standard library only.
"""
import functools
import http.server
import socket
import sys
import threading
import webbrowser
from pathlib import Path

GUIDE = Path(__file__).resolve().parent.parent / "hardware" / "guide"


def lan_ip():
    """This machine's address on the local network (no packets are sent)."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("10.255.255.255", 1))
            return s.getsockname()[0]
        except OSError:
            return None


def main():
    args = sys.argv[1:]
    lan = "--lan" in args
    ports = [a for a in args if a != "--lan"]
    port = int(ports[0]) if ports else 8770
    if not any((GUIDE / "models").glob("split-flap-*.json")):
        print("No 3D models in hardware/guide/models/. Run python tools/make_guide_models.py for the diagram.")
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(GUIDE))
    server = http.server.ThreadingHTTPServer(("0.0.0.0" if lan else "127.0.0.1", port), handler)
    url = f"http://127.0.0.1:{port}/"
    print(f"Build guide at {url} (Ctrl+C to stop)")
    if lan:
        ip = lan_ip()
        if ip:
            print(f"On other devices on this network: http://{ip}:{port}/")
        else:
            print("Couldn't find this machine's network address; check ipconfig.")
    threading.Timer(0.5, webbrowser.open, [url]).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
