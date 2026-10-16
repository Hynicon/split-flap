"""One-time OAuth bootstrap for the split-flap follower counter.

Runs the official login flow on your PC, exchanges for long-lived tokens and POSTs them to the
device (http://splitflap.local/api/tokens). Tokens are never written to disk.

    cd tools
    python -m auth instagram|facebook|tiktok [--device HOST] [--dry-run]
"""
