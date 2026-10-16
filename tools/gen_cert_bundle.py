"""Generate the ESP32 x509 certificate bundle from the Mozilla CA list (via certifi).

The output format is the one read by arduino-esp32's esp_crt_bundle.c:
  u16 BE cert count, then per cert: u16 BE subject length, u16 BE key length,
  subject DER, SubjectPublicKeyInfo DER. Certs are sorted by subject DER (the firmware binary-searches).

Usage:  py -m pip install --user certifi cryptography
        py tools/gen_cert_bundle.py            # writes firmware/master/certs/x509_crt_bundle.bin
Re-run occasionally (e.g. yearly) to pick up CA changes, then re-flash.
"""

import pathlib
import struct
import sys

import certifi
from cryptography import x509
from cryptography.hazmat.primitives import serialization

OUT = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "master" / "certs" / "x509_crt_bundle.bin"


def main() -> int:
    pem = pathlib.Path(certifi.where()).read_bytes()
    certs = x509.load_pem_x509_certificates(pem)
    entries = []
    for cert in certs:
        name = cert.subject.public_bytes()
        key = cert.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
        entries.append((name, key))
    entries.sort(key=lambda e: e[0])

    out = bytearray(struct.pack(">H", len(entries)))
    for name, key in entries:
        out += struct.pack(">HH", len(name), len(key)) + name + key
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(out)
    print(f"{len(entries)} CA certificates from certifi {certifi.__version__} -> {OUT} ({len(out)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
