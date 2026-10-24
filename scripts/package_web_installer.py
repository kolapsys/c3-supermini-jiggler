from __future__ import annotations

import json
import os
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

    DIST.mkdir(exist_ok=True)
    (DIST / "firmware.bin").write_bytes(firmware)
    shutil.copyfile(ROOT / "web-installer" / "index.html", DIST / "index.html")
    manifest = {
        "name": "C3 Mouse Jiggler",
        "version": "1.0.0",
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
    print(f"Packaged {len(firmware)} bytes into {DIST / 'firmware.bin'}")


if __name__ == "__main__":
    main()
