import os
import sys
from pathlib import Path


def find_esptool_package_dir() -> Path:
    candidates = []

    packages_dir = os.environ.get("PLATFORMIO_PACKAGES_DIR")
    if packages_dir:
        candidates.append(Path(packages_dir) / "tool-esptoolpy")

    core_dir = os.environ.get("PLATFORMIO_CORE_DIR")
    if core_dir:
        candidates.append(Path(core_dir) / "packages" / "tool-esptoolpy")

    candidates.append(Path.home() / ".platformio" / "packages" / "tool-esptoolpy")

    for candidate in candidates:
        if (candidate / "esptool" / "__init__.py").exists():
            return candidate

    raise RuntimeError("PlatformIO tool-esptoolpy package was not found")


sys.path.insert(0, str(find_esptool_package_dir()))

import esptool  # noqa: E402


if __name__ == "__main__":
    esptool._main()
