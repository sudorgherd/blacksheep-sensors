"""Select and verify ESP-IDF's native ESP32-C5 bootloader artifact.

PlatformIO's ESP-IDF integration currently regenerates the bootloader from its
ELF without the chip-revision arguments used by ESP-IDF's CMake build.  Keep the
normal PlatformIO workflow, but replace that generated upload artifact with the
authoritative CMake output and fail closed if its identity or header changes.
"""

from hashlib import sha256
from os import pathsep
from pathlib import Path
from shutil import copyfile
from struct import unpack_from
from subprocess import run

Import("env")


EXPECTED = {
    "magic": 0xE9,
    "flash_mode": 0x02,       # DIO
    "flash_size_freq": 0x5F,  # 32 MB, 80 MHz on ESP32-C5
    "chip_id": 23,            # ESP32-C5
    "min_rev_full": 100,      # v1.0
    "max_rev_full": 199,      # v1.99
}


def _paths():
    build_dir = Path(env.subst("$BUILD_DIR"))
    return build_dir / "bootloader" / "bootloader.bin", build_dir / "bootloader.bin"


def _digest(path):
    return sha256(path.read_bytes()).hexdigest()


def _read_header(path):
    data = path.read_bytes()
    if len(data) < 24:
        raise RuntimeError(f"bootloader is too short to contain an ESP image header: {path}")

    return {
        "magic": data[0],
        "flash_mode": data[2],
        "flash_size_freq": data[3],
        "chip_id": unpack_from("<H", data, 12)[0],
        "min_rev_full": unpack_from("<H", data, 15)[0],
        "max_rev_full": unpack_from("<H", data, 17)[0],
    }


def _verify(native, selected):
    if not native.is_file():
        raise RuntimeError(
            "native ESP-IDF CMake bootloader is missing: "
            f"{native}. Refusing to use PlatformIO's regenerated bootloader."
        )
    if not selected.is_file():
        raise RuntimeError(f"selected upload bootloader is missing: {selected}")

    native_sha = _digest(native)
    selected_sha = _digest(selected)
    if native_sha != selected_sha:
        raise RuntimeError(
            "bootloader regression guard failed: the upload artifact does not match "
            f"ESP-IDF's native CMake bootloader (native={native_sha}, selected={selected_sha})"
        )

    header = _read_header(selected)
    mismatches = [
        f"{name}=0x{header[name]:x} (expected 0x{expected:x})"
        for name, expected in EXPECTED.items()
        if header[name] != expected
    ]
    if mismatches:
        raise RuntimeError(
            "bootloader regression guard failed: " + ", ".join(mismatches)
        )

    print(
        "BLACKSHEEP bootloader guard: PASS\n"
        f"  native:   {native_sha}\n"
        f"  selected: {selected_sha}\n"
        "  image: ESP32-C5, DIO, 80 MHz, 32 MB, revisions v1.0-v1.99"
    )


def build_native_bootloader(target, source, env):
    build_dir = Path(env.subst("$BUILD_DIR")) / "bootloader"
    cmake_dir = Path(env.PioPlatform().get_package_dir("tool-cmake")) / "bin"
    cmake = cmake_dir / "cmake.exe"
    if not cmake.is_file():
        raise RuntimeError(f"PlatformIO CMake executable is missing: {cmake}")

    process_env = dict(env["ENV"])
    esptool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
    if not esptool_dir:
        raise RuntimeError("PlatformIO esptool package is unavailable")
    platformio_site_packages = (
        Path(env.subst("$PYTHONEXE")).parent.parent / "Lib" / "site-packages"
    )
    prior_pythonpath = process_env.get("PYTHONPATH", "")
    process_env["PYTHONPATH"] = pathsep.join(
        [str(esptool_dir), str(platformio_site_packages)]
    ) + (
        pathsep + prior_pythonpath if prior_pythonpath else ""
    )

    result = run(
        [str(cmake), "--build", str(build_dir)],
        env=process_env,
    )
    if result.returncode:
        raise RuntimeError(
            "native ESP-IDF CMake bootloader build failed; refusing to use "
            "PlatformIO's regenerated artifact"
        )


def select_native_bootloader(source, target, env):
    native, selected = _paths()
    if not native.is_file():
        raise RuntimeError(
            "ESP-IDF did not produce its native CMake bootloader at "
            f"{native}; refusing to retain PlatformIO's regenerated artifact"
        )
    copyfile(native, selected)
    _verify(native, selected)


def verify_upload_bootloader(source, target, env):
    _verify(*_paths())


native_path, selected_path = _paths()
native_node = env.Command(
    str(native_path),
    ["$BUILD_DIR/bootloader/build.ninja", "$PROJECT_DIR/sdkconfig.$PIOENV"],
    build_native_bootloader,
)
env.Depends("$BUILD_DIR/bootloader.bin", native_node)
env.Depends("$BUILD_DIR/bootloader.elf", native_node)
env.AddPostAction("$BUILD_DIR/bootloader.bin", select_native_bootloader)
env.AddPreAction("upload", verify_upload_bootloader)
