#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0


"""Prepare a tiny compile SDK for PyPI ``usd-core`` wheels.

The PyPI package is a runtime wheel: it ships Python modules, plugin metadata,
and the monolithic USD shared library, but not C++ headers or CMake exports.
This helper combines the wheel with matching OpenUSD source headers to create
just enough of an SDK for building binary plugins against the wheel's
``libusd_ms`` SONAME.
"""

from __future__ import annotations

import argparse
import os
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import struct
import zipfile

OPENUSD_REPO = "https://github.com/PixarAnimationStudios/OpenUSD.git"
TBB_REPO = "https://github.com/oneapi-src/oneTBB.git"
DEFAULT_TBB_TAG = "v2020.3"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--usd-core-version", required=True)
    parser.add_argument("--openusd-tag", required=True)
    parser.add_argument("--sdk-root", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--python-executable", default=sys.executable)
    parser.add_argument("--tbb-tag", default=DEFAULT_TBB_TAG)
    parser.add_argument("--cxx11-abi", choices=("0", "1"), default="")
    parser.add_argument("--openusd-source", type=Path)
    parser.add_argument("--tbb-source", type=Path)
    args = parser.parse_args()

    work_dir = args.work_dir.resolve()
    sdk_root = args.sdk_root.resolve()
    downloads_dir = work_dir / "downloads"
    wheel_extract = work_dir / "usd-core-wheel"
    openusd_source = (args.openusd_source or work_dir / f"OpenUSD-{args.openusd_tag}").resolve()
    tbb_source = (args.tbb_source or work_dir / f"oneTBB-{args.tbb_tag}").resolve()

    downloads_dir.mkdir(parents=True, exist_ok=True)
    _download_usd_core(args.python_executable, args.usd_core_version, downloads_dir)
    wheel_path = _single_wheel(downloads_dir, args.usd_core_version)

    _clone_if_needed(OPENUSD_REPO, args.openusd_tag, openusd_source)
    _clone_if_needed(TBB_REPO, args.tbb_tag, tbb_source)

    _reset_dir(wheel_extract)
    with zipfile.ZipFile(wheel_path) as archive:
        archive.extractall(wheel_extract)

    _clean_usd_sdk_overlay(sdk_root)
    _copytree(openusd_source / "pxr", sdk_root / "include" / "pxr")
    _copytree(tbb_source / "include" / "tbb", sdk_root / "include" / "tbb")

    version = _parse_openusd_tag(args.openusd_tag)
    _write_pxr_h(sdk_root / "include" / "pxr" / "pxr.h", version)
    _write_work_impl_h(sdk_root / "include" / "pxr" / "base" / "work" / "impl.h")
    _prepare_usdgen_resources(openusd_source, wheel_extract)
    _write_usdgen_wrapper(sdk_root / "bin" / "usdGenSchema", wheel_extract)
    _write_cmake_package(sdk_root, wheel_extract, version, args.cxx11_abi)

    print(f"Prepared usd-core SDK shim: {sdk_root}")
    print(f"Extracted usd-core wheel: {wheel_extract}")
    return 0


def _run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def _download_usd_core(python_executable: str, version: str, downloads_dir: Path) -> None:
    existing = list(downloads_dir.glob(f"usd_core-{version}-*.whl"))
    if existing:
        return
    _run(
        [
            python_executable,
            "-m",
            "pip",
            "download",
            "--only-binary=:all:",
            "--no-deps",
            "--dest",
            os.fspath(downloads_dir),
            f"usd-core=={version}",
        ]
    )


def _single_wheel(downloads_dir: Path, version: str) -> Path:
    wheels = sorted(downloads_dir.glob(f"usd_core-{version}-*.whl"))
    if len(wheels) != 1:
        raise SystemExit(f"Expected one usd-core {version} wheel in {downloads_dir}, found {len(wheels)}")
    return wheels[0]


def _clone_if_needed(repo: str, tag: str, destination: Path) -> None:
    if (destination / ".git").is_dir():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    _run(["git", "clone", "--depth", "1", "--branch", tag, repo, os.fspath(destination)])


def _reset_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def _clean_usd_sdk_overlay(sdk_root: Path) -> None:
    """Remove only files owned by this usd-core shim.

    The superbuild may run this helper in an existing SDK prefix. Deleting the
    whole prefix would erase unrelated files and make incremental rebuilds
    fragile, so keep cleanup scoped to paths this helper creates.
    """

    sdk_root.mkdir(parents=True, exist_ok=True)
    for path in (
        sdk_root / "include" / "pxr",
        sdk_root / "include" / "tbb",
        sdk_root / "bin" / "usdGenSchema",
        sdk_root / "pxrConfig.cmake",
        sdk_root / "cmake" / "pxrTargets.cmake",
        sdk_root / "lib" / "usd_ms.def",
        sdk_root / "lib" / "usd_ms.lib",
        sdk_root / "lib" / "tbb.def",
        sdk_root / "lib" / "tbb.lib",
    ):
        _remove_path(path)


def _remove_path(path: Path) -> None:
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def _copytree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise SystemExit(f"Required source directory does not exist: {source}")
    shutil.copytree(source, destination, dirs_exist_ok=True)


def _parse_openusd_tag(tag: str) -> tuple[int, int]:
    value = tag.removeprefix("v")
    parts = value.split(".")
    if len(parts) != 2:
        raise SystemExit(f"Unsupported OpenUSD tag format: {tag}")
    return int(parts[0]), int(parts[1])


def _write_pxr_h(path: Path, version: tuple[int, int]) -> None:
    minor, patch = version
    pxr_version = minor * 100 + patch
    internal_ns = f"pxrInternal_v0_{minor}_{patch}__pxrReserved__"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"""\
#ifndef PXR_H
#define PXR_H

#define PXR_MAJOR_VERSION 0
#define PXR_MINOR_VERSION {minor}
#define PXR_PATCH_VERSION {patch}

#define PXR_VERSION {pxr_version}

#define PXR_USE_NAMESPACES 1

#if PXR_USE_NAMESPACES
#define PXR_NS pxr
#define PXR_INTERNAL_NS {internal_ns}
#define PXR_NS_GLOBAL ::PXR_NS

namespace PXR_INTERNAL_NS {{ }}
namespace PXR_NS {{
    using namespace PXR_INTERNAL_NS;
}}

#define PXR_NAMESPACE_OPEN_SCOPE namespace PXR_INTERNAL_NS {{
#define PXR_NAMESPACE_CLOSE_SCOPE }}
#define PXR_NAMESPACE_USING_DIRECTIVE using namespace PXR_NS;
#else
#define PXR_NS
#define PXR_NS_GLOBAL
#define PXR_NAMESPACE_OPEN_SCOPE
#define PXR_NAMESPACE_CLOSE_SCOPE
#define PXR_NAMESPACE_USING_DIRECTIVE
#endif

#define PXR_PYTHON_SUPPORT_ENABLED
#define PXR_PREFER_SAFETY_OVER_SPEED
#define PXR_USE_INTERNAL_BOOST_PYTHON

#endif
""",
        encoding="utf-8",
    )


def _write_work_impl_h(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        """\
#ifndef PXR_BASE_WORK_IMPL_H
#define PXR_BASE_WORK_IMPL_H

#include "pxr/base/work/workTBB/impl.h"

#ifdef WORK_IMPL_NS
#define PXR_WORK_IMPL_NS WORK_IMPL_NS
#define PXR_WORK_IMPL_NAMESPACE_USING_DIRECTIVE using namespace PXR_WORK_IMPL_NS;
#else
#define PXR_WORK_IMPL_NS
#define PXR_WORK_IMPL_NAMESPACE_USING_DIRECTIVE
#endif

#endif
""",
        encoding="utf-8",
    )


def _prepare_usdgen_resources(openusd_source: Path, wheel_extract: Path) -> None:
    resources = wheel_extract / "pxr" / "pluginfo" / "usd" / "resources"
    if not resources.is_dir():
        raise SystemExit(f"usd-core wheel is missing USD plugin resources: {resources}")
    schema_source = openusd_source / "pxr" / "usd" / "usd" / "schema.usda"
    templates_source = openusd_source / "pxr" / "usd" / "usd" / "codegenTemplates"
    if not schema_source.is_file():
        raise SystemExit(f"OpenUSD source is missing schema.usda: {schema_source}")
    if not templates_source.is_dir():
        raise SystemExit(f"OpenUSD source is missing codegenTemplates: {templates_source}")

    nested_schema = resources / "usd" / "schema.usda"
    nested_schema.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(schema_source, nested_schema)
    shutil.copy2(schema_source, resources / "schema.usda")
    _copytree(templates_source, resources / "codegenTemplates")


def _write_usdgen_wrapper(path: Path, wheel_extract: Path) -> None:
    script = wheel_extract / "pxr" / "Usd" / "usdGenSchema.py"
    if not script.is_file():
        raise SystemExit(f"usd-core wheel is missing usdGenSchema.py: {script}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"""\
#!/usr/bin/env python3
import os
import runpy
import sys
from pathlib import Path

wheel_root = Path({str(wheel_extract)!r})
os.environ["PXR_PLUGINPATH_NAME"] = str(wheel_root / "pxr" / "pluginfo")
if os.name == "nt":
    dll_dir = wheel_root / "pxr"
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(dll_dir))
    os.environ["PATH"] = str(dll_dir) + os.pathsep + os.environ.get("PATH", "")
sys.path.insert(0, str(wheel_root))
runpy.run_path(str(wheel_root / "pxr" / "Usd" / "usdGenSchema.py"), run_name="__main__")
""",
        encoding="utf-8",
    )
    path.chmod(0o755)


def _write_cmake_package(
    sdk_root: Path,
    wheel_extract: Path,
    version: tuple[int, int],
    cxx11_abi: str,
) -> None:
    minor, patch = version
    usd_version = f"0.{minor}.{patch}"
    target_name, libusd, import_lib = _usd_library_inputs(sdk_root, wheel_extract)
    cmake_dir = sdk_root / "cmake"
    cmake_dir.mkdir(parents=True, exist_ok=True)

    (sdk_root / "pxrConfig.cmake").write_text(
        f"""\
set(PXR_MAJOR_VERSION "0")
set(PXR_MINOR_VERSION "{minor}")
set(PXR_PATCH_VERSION "{patch}")
set(PXR_VERSION "{minor * 100 + patch}")
set(PXR_INCLUDE_DIRS "${{CMAKE_CURRENT_LIST_DIR}}/include")
include("${{CMAKE_CURRENT_LIST_DIR}}/cmake/pxrTargets.cmake")
set(PXR_LIBRARIES {target_name})
""",
        encoding="utf-8",
    )

    compile_definitions = ""
    if cxx11_abi:
        compile_definitions = f'\n  INTERFACE_COMPILE_DEFINITIONS "_GLIBCXX_USE_CXX11_ABI={cxx11_abi}"'

    (cmake_dir / "pxrTargets.cmake").write_text(
        f"""\
# Generated by cmake/superbuild/scripts/prepare_usdcore_sdk.py for usd-core/OpenUSD {usd_version}.

if(TARGET {target_name} AND TARGET python)
  return()
endif()

get_filename_component(_IMPORT_PREFIX "${{CMAKE_CURRENT_LIST_FILE}}" PATH)
get_filename_component(_IMPORT_PREFIX "${{_IMPORT_PREFIX}}" PATH)

add_library({target_name} SHARED IMPORTED)
set_target_properties({target_name} PROPERTIES
  IMPORTED_LOCATION "{_cmake_path(libusd)}"{_import_lib_property(import_lib)}
  INTERFACE_INCLUDE_DIRECTORIES "${{_IMPORT_PREFIX}}/include"{_link_dirs_property(import_lib)}{compile_definitions}
)

add_library(python INTERFACE IMPORTED)
set_target_properties(python PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${{_IMPORT_PREFIX}}/include"
)
""",
        encoding="utf-8",
    )


def _usd_library_inputs(sdk_root: Path, wheel_extract: Path) -> tuple[str, Path, Path | None]:
    linux_libs = wheel_extract / "usd_core.libs"
    if linux_libs.is_dir():
        return "usd_m", _single_file(linux_libs, "libusd_ms*.so"), None

    windows_dll = wheel_extract / "pxr" / "usd_ms.dll"
    if windows_dll.is_file():
        import_lib = sdk_root / "lib" / "usd_ms.lib"
        _write_windows_import_library(windows_dll, import_lib)
        tbb_dll = wheel_extract / "pxr" / "tbb.dll"
        if tbb_dll.is_file():
            _write_windows_import_library(tbb_dll, sdk_root / "lib" / "tbb.lib")
        return "usd_ms", windows_dll, import_lib

    raise SystemExit(f"Could not find a usd-core monolithic USD library under {wheel_extract}")


def _import_lib_property(import_lib: Path | None) -> str:
    if not import_lib:
        return ""
    return f'\n  IMPORTED_IMPLIB "{_cmake_path(import_lib)}"'


def _link_dirs_property(import_lib: Path | None) -> str:
    if not import_lib:
        return ""
    return '\n  INTERFACE_LINK_DIRECTORIES "${_IMPORT_PREFIX}/lib"'


def _write_windows_import_library(dll_path: Path, import_lib: Path) -> None:
    if os.name != "nt":
        raise SystemExit(
            "Windows usd-core wheels require a generated MSVC import library. "
            "Run this helper on Windows so lib.exe is available."
        )

    exports = _read_pe_exports(dll_path)
    if not exports:
        raise SystemExit(f"No exports found in {dll_path}")

    import_lib.parent.mkdir(parents=True, exist_ok=True)
    def_file = import_lib.with_suffix(".def")
    def_file.write_text(
        "\n".join(
            [
                f"LIBRARY {dll_path.name}",
                "EXPORTS",
                *[f"    {name}" for name in exports],
                "",
            ]
        ),
        encoding="utf-8",
    )

    lib_tool = _find_msvc_lib_tool()
    _run(
        [
            lib_tool,
            "/NOLOGO",
            f"/MACHINE:{_msvc_machine()}",
            f"/DEF:{os.fspath(def_file)}",
            f"/OUT:{os.fspath(import_lib)}",
        ]
    )


def _find_msvc_lib_tool() -> str:
    lib_tool = shutil.which("lib.exe") or shutil.which("lib")
    if lib_tool:
        return lib_tool

    vswhere_candidates = [
        os.environ.get("VSWHERE"),
        r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe",
        r"C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe",
    ]
    for candidate in vswhere_candidates:
        if not candidate or not Path(candidate).is_file():
            continue
        result = subprocess.run(
            [
                candidate,
                "-latest",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-find",
                r"VC\Tools\MSVC\*\bin\Hostx64\x64\lib.exe",
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
        )
        tools = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        if tools:
            return tools[-1]

    raise SystemExit("Could not find MSVC lib.exe to generate usd_ms.lib")


def _msvc_machine() -> str:
    machine = platform.machine().lower()
    if machine in {"amd64", "x86_64"}:
        return "X64"
    if machine in {"arm64", "aarch64"}:
        return "ARM64"
    raise SystemExit(f"Unsupported Windows machine for import library generation: {machine}")


def _read_pe_exports(dll_path: Path) -> list[str]:
    data = dll_path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise SystemExit(f"Not a PE file: {dll_path}")

    pe_offset = _unpack_from("<I", data, 0x3C)
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise SystemExit(f"Missing PE signature: {dll_path}")

    coff_offset = pe_offset + 4
    section_count = _unpack_from("<H", data, coff_offset + 2)
    optional_header_size = _unpack_from("<H", data, coff_offset + 16)
    optional_offset = coff_offset + 20
    magic = _unpack_from("<H", data, optional_offset)
    if magic == 0x10B:
        data_directory_offset = optional_offset + 96
    elif magic == 0x20B:
        data_directory_offset = optional_offset + 112
    else:
        raise SystemExit(f"Unsupported PE optional header magic {magic:#x}: {dll_path}")

    export_rva = _unpack_from("<I", data, data_directory_offset)
    if export_rva == 0:
        return []

    section_offset = optional_offset + optional_header_size
    sections = []
    for index in range(section_count):
        offset = section_offset + index * 40
        virtual_size = _unpack_from("<I", data, offset + 8)
        virtual_address = _unpack_from("<I", data, offset + 12)
        raw_size = _unpack_from("<I", data, offset + 16)
        raw_pointer = _unpack_from("<I", data, offset + 20)
        size = max(virtual_size, raw_size)
        sections.append((virtual_address, size, raw_pointer))

    export_offset = _rva_to_offset(export_rva, sections)
    if export_offset is None:
        raise SystemExit(f"Could not map PE export table RVA for {dll_path}")

    export_fields = struct.unpack_from("<IIHHIIIIIII", data, export_offset)
    name_count = export_fields[7]
    address_of_names = export_fields[9]
    names_offset = _rva_to_offset(address_of_names, sections)
    if names_offset is None:
        raise SystemExit(f"Could not map PE export names RVA for {dll_path}")

    names = []
    for index in range(name_count):
        name_rva = _unpack_from("<I", data, names_offset + index * 4)
        name_offset = _rva_to_offset(name_rva, sections)
        if name_offset is None:
            continue
        names.append(_read_c_string(data, name_offset))
    return sorted(set(names))


def _rva_to_offset(rva: int, sections: list[tuple[int, int, int]]) -> int | None:
    for virtual_address, size, raw_pointer in sections:
        if virtual_address <= rva < virtual_address + size:
            return raw_pointer + (rva - virtual_address)
    return None


def _read_c_string(data: bytes, offset: int) -> str:
    end = data.index(b"\0", offset)
    return data[offset:end].decode("ascii")


def _unpack_from(format_string: str, data: bytes, offset: int) -> int:
    return struct.unpack_from(format_string, data, offset)[0]


def _single_file(directory: Path, pattern: str) -> Path:
    matches = sorted(directory.glob(pattern))
    if len(matches) != 1:
        raise SystemExit(f"Expected one {pattern} under {directory}, found {len(matches)}")
    return matches[0]


def _cmake_path(path: Path) -> str:
    return os.fspath(path).replace("\\", "/")


if __name__ == "__main__":
    raise SystemExit(main())
