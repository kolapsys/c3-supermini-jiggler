from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".pio" / "build" / "esp32-c3-supermini"
DIST = ROOT / "dist"
PLATFORMIO_HOME = Path(
    os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio"))
)
FLASH_SIZE = 4 * 1024 * 1024
FLASH_IMAGES = (
    (0x0000, BUILD / "bootloader.bin"),
    (0x8000, BUILD / "partitions.bin"),
    (
        0xE000,
        PLATFORMIO_HOME
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "partitions"
        / "boot_app0.bin",
    ),
    (0x10000, BUILD / "firmware.bin"),
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--version",
        default=os.environ.get("FIRMWARE_VERSION", "dev-local"),
        help="firmware release version (for example v1.2.0)",
    )
    args = parser.parse_args()
    if args.version.startswith("v") and not re.fullmatch(
        r"v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
        r"(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?",
        args.version,
    ):
        parser.error("tag versions must use semantic version format, such as v1.2.3")

    images: list[tuple[int, bytes]] = []
    image_end = 0
    for offset, path in FLASH_IMAGES:
        if not path.is_file():
            raise FileNotFoundError(f"Required firmware image not found: {path}")
        data = path.read_bytes()
        if offset < image_end:
            raise ValueError(f"Firmware image overlaps at offset 0x{offset:x}")
        image_end = offset + len(data)
        if image_end > FLASH_SIZE:
            raise ValueError(f"Firmware image exceeds {FLASH_SIZE // (1024 * 1024)} MB flash")
        images.append((offset, data))

    firmware = bytearray(b"\xff" * image_end)
    for offset, data in images:
        firmware[offset : offset + len(data)] = data

    manifest_version = args.version
    if manifest_version.startswith("v"):
        manifest_version = manifest_version[1:]
    elif manifest_version.startswith("dev-"):
        manifest_version = f"0.0.0-{manifest_version[4:]}"

    DIST.mkdir(exist_ok=True)
    firmware_path = DIST / "firmware.bin"
    firmware_path.write_bytes(firmware)
    shutil.copyfile(ROOT / "web-installer" / "index.html", DIST / "index.html")
    manifest = {
        "name": "ESP32-C3 Super Mini Jiggler",
        "version": manifest_version,
        "builds": [
            {
                "chipFamily": "ESP32-C3",
                "parts": [{"path": "firmware.bin", "offset": 0}],
            }
        ],
    }
    (DIST / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    (DIST / "version.json").write_text(
        json.dumps({"version": args.version}, indent=2) + "\n", encoding="utf-8"
    )
    checksum = hashlib.sha256(firmware).hexdigest()
    (DIST / "firmware.bin.sha256").write_text(
        f"{checksum}  firmware.bin\n", encoding="ascii"
    )
    print(f"Packaged version {args.version}: {len(firmware)} bytes into {firmware_path}")


if __name__ == "__main__":
    main()
