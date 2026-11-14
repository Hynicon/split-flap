"""Pack the server's build context into one .tar for Portainer's "Build a new image" (Upload).

The archive mirrors the repository layout (Dockerfile at the top, server/ and
firmware/master/data/ inside), so the same server/Dockerfile works unchanged.

    python server/make_bundle.py        # writes server/dist/splitflap-server.tar
"""
import io
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "server" / "dist" / "splitflap-server.tar"
FILES = [
    ("server/Dockerfile", "Dockerfile"),
    ("server/requirements.txt", "server/requirements.txt"),
    ("server/app.py", "server/app.py"),
]
STATIC = ROOT / "firmware" / "master" / "data"


def add(tar: tarfile.TarFile, src: Path, name: str) -> None:
    data = src.read_bytes()
    info = tarfile.TarInfo(name)
    info.size, info.mode = len(data), 0o644
    tar.addfile(info, io.BytesIO(data))


def main() -> None:
    OUT.parent.mkdir(exist_ok=True)
    with tarfile.open(OUT, "w") as tar:
        for src, name in FILES:
            add(tar, ROOT / src, name)
        for f in sorted(STATIC.iterdir()):
            if f.is_file():
                add(tar, f, f"firmware/master/data/{f.name}")
    names = tarfile.open(OUT).getnames()
    print(f"{OUT.relative_to(ROOT)}: {OUT.stat().st_size // 1024} KB, {len(names)} files")


if __name__ == "__main__":
    main()
