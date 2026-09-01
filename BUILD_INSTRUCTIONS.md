# cae-openusd-plugins Build Instructions (Windows)

Two supported build recipes, selected by how the artifact will be consumed:

| Consumer | USD flavor | Recipe |
| --- | --- | --- |
| **kit-cae 3.x / Omniverse Kit** (`KIT_CAE_OPENUSD_PLUGINS_PACKAGE` override) | OpenUSD split libs matching kit's runtime | [Recipe A — kit-cae compatible](#recipe-a--kit-cae-compatible-build) |
| Standalone Python (usd-core), CI wheel | Monolithic `usd_ms.dll` | [Recipe B — usd-core / wheel build](#recipe-b--usd-core--wheel-build) |

> **Do not mix them.** A monolithic-USD-linked plugin package cannot be loaded
> by kit-cae; `LoadLibrary` fails with *The specified module could not be
> found* because kit ships `usd_ar.dll`, `usd_sdf.dll`, ... rather than
> `usd_ms.dll`. Kit-cae's `use_local_dependencies.py` will link the package,
> but plugins will not load at runtime. Use Recipe A for anything consumed
> by kit-cae.

---

## Prerequisites (both recipes)

- Visual Studio 2022 (Community or higher) with the **Desktop development
  with C++** workload installed. Verify with:
  ```powershell
  Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC" -Directory
  ```
  If the directory is empty or the `Common7\Tools\Launch-VsDevShell.ps1`
  script is missing, run the VS Installer → *Repair* before continuing.
- Windows 10/11 SDK (any version under `C:\Program Files (x86)\Windows Kits\10`).
- Git.
- [miniforge3](https://github.com/conda-forge/miniforge) or another Python
  3.12 install with `usd-core` (used for `usdGenSchema` in Recipe B and for
  running the standalone tests):
  ```powershell
  pip install usd-core==25.11
  ```

### Enable Windows long-path support (one-time, elevated)

```powershell
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" `
    -Name "LongPathsEnabled" -Value 1
git config --global core.longpaths true
```

---

## Environment setup (used by both recipes)

Open a **new** PowerShell window. Always start by clearing `PYTHONPATH` — a
stray value poisons `cmake -E env` because the Windows `;` path separator
collides with CMake's list separator:

```powershell
if (Test-Path Env:PYTHONPATH) { Remove-Item Env:PYTHONPATH }
```

Discover the exact MSVC toolset and Windows SDK versions installed on this
machine (they may differ from the values shown in older docs):

```powershell
$msvcRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC'
$msvcVer  = (Get-ChildItem $msvcRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$sdkRoot  = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVer   = (Get-ChildItem "$sdkRoot\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
"MSVC $msvcVer, Windows SDK $sdkVer"
```

Point PATH at the internal cmake shipped by the format-deps superbuild (if
present), the winget ninja, and the compiler:

```powershell
$env:PATH = "$PWD\_build\tools\cmake\data\bin;" +
            (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Ninja-build.Ninja_*").FullName + ";" +
            "$msvcRoot\$msvcVer\bin\Hostx64\x64;" +
            "$sdkRoot\bin\$sdkVer\x64;" +
            $env:PATH
$env:INCLUDE = "$msvcRoot\$msvcVer\include;" +
               "$sdkRoot\Include\$sdkVer\ucrt;" +
               "$sdkRoot\Include\$sdkVer\shared;" +
               "$sdkRoot\Include\$sdkVer\um;" +
               "$sdkRoot\Include\$sdkVer\winrt"
$env:LIB     = "$msvcRoot\$msvcVer\lib\x64;" +
               "$sdkRoot\Lib\$sdkVer\ucrt\x64;" +
               "$sdkRoot\Lib\$sdkVer\um\x64"
cmake --version
cl.exe 2>&1 | Select-Object -First 1
```

You should see cmake 3.28 or newer and a `Microsoft (R) C/C++ Optimizing
Compiler` banner.

> **Why not `Launch-VsDevShell.ps1`?** On installs where the VS Developer
> Shell script is missing (older repair states, partial workload installs),
> the environment prep above is a self-contained replacement.

---

## Recipe A — kit-cae compatible build

This recipe reuses kit-cae 3.x's pre-built **split-libs OpenUSD SDK**
(from `kit-cae-3.0/_build/target-deps/usd/release/`) as the compile SDK.
This bypasses `build_usd.py` entirely and guarantees ABI compatibility with
kit's runtime. Total build time: ~2 minutes after the format-deps SDK is
present.

### A1. Format-dependency SDK

Run the format-only side of the superbuild once. It downloads and builds
pugixml, LZ4, zlib, xz/liblzma, HDF5, and CGNS into `_build/sdk/`. This does
**not** invoke `build_usd.py`.

```powershell
cmake "-DCAE_USD_FLAVOR=usd-core" "-DCAE_USD_VERSION=25.11" -P cmake/ci/superbuild.cmake
```

> **PowerShell quoting matters.** `25.11` must be quoted so PowerShell does
> not truncate it to `25`.

The step above also creates `_build/sdk_usd/` with a usd-core stub. **Delete
it** — Recipe A uses kit-cae's USD SDK instead:

```powershell
Remove-Item _build\sdk_usd -Recurse -Force -ErrorAction SilentlyContinue
New-Item _build\sdk_usd -ItemType Directory | Out-Null
```

Point the plugin build at kit's USD SDK by dropping a hand-written cache
fragment at `_build/sdk_usd/cae-usd-sdk-cache.cmake`:

```cmake
# _build/sdk_usd/cae-usd-sdk-cache.cmake
set(_CAE_KIT_USD_ROOT "C:/Users/<you>/Documents/OV-Composer/kit-cae-3.0/_build/target-deps/usd/release")

set(USD_ROOT "${_CAE_KIT_USD_ROOT}" CACHE PATH
    "Kit-cae USD SDK (split libraries, matching kit runtime)")

if(WIN32)
    set(_SEP "\;")
else()
    set(_SEP ":")
endif()
if(DEFINED ENV{CMAKE_PREFIX_PATH} AND NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
    set(ENV{CMAKE_PREFIX_PATH} "$ENV{CMAKE_PREFIX_PATH}${_SEP}${_CAE_KIT_USD_ROOT}")
else()
    set(ENV{CMAKE_PREFIX_PATH} "${_CAE_KIT_USD_ROOT}")
endif()
set(CAE_USD_VERSION "0.25.11" CACHE STRING "OpenUSD version reported by the linked SDK")
```

### A2. Configure

```powershell
$cl = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\$msvcVer\bin\Hostx64\x64\cl.exe"
cmake -S . -B build -G Ninja `
  "-DCMAKE_C_COMPILER=$cl" `
  "-DCMAKE_CXX_COMPILER=$cl" `
  -C _build/sdk/cae-format-sdk-cache.cmake `
  -C _build/sdk_usd/cae-usd-sdk-cache.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  -DCAE_PACKAGE_VARIANT=openusd `
  -DCAE_ENABLE_EDEM=ON `
  -DCAE_ENABLE_VTKHDF=ON `
  -DCAE_ENABLE_VTK=OFF `
  -DCAE_ENABLE_CGNS=OFF `
  -DCAE_ENABLE_ECLIPSE=OFF `
  -DCAE_ENABLE_FLASH=OFF `
  -DCAE_ENABLE_OPENFOAM=OFF `
  -DCAE_ENABLE_ENSIGHT=OFF `
  -DCAE_ENABLE_NUMPY=OFF `
  -DCAE_ENABLE_TRIMESH=OFF `
  -DCAE_ENABLE_NVDB=OFF `
  -DCAE_ENABLE_PYTHON_PROXY=OFF
```

> **`CAE_PACKAGE_VARIANT=openusd`** stamps the produced package with the same
> variant metadata as kit-cae's published `cae_openusd_plugins` artifact.
> Without it, `use_local_dependencies.py` rejects the override with
> `Local cae_openusd_plugins package is incompatible: CAE_PACKAGE_VARIANT:
> local='', expected='openusd'`.

> **Optional plugins are disabled** in the invocation above because they
> pull in more dependencies (VTK, CGNS, LZMA) that occasionally re-detect
> differently against kit's USD headers. Enable individually as needed.

### A3. Build, install, and package

```powershell
$env:PATH = "$PWD\_build\sdk\bin;" + $env:PATH  # HDF5 dlls for schema gen

cmake --build build --parallel
cmake --build build --target install
cmake --build build --target package
```

The archive lands at:

```
build\packages\cae_openusd_plugins@0.1.1+openusd.usd-0.25.11.py312.windows-x86_64.<rev>.zip
```

### A4. Deploy into kit-cae

Kit-cae's `tools/use_local_dependencies.py` reads
`KIT_CAE_OPENUSD_PLUGINS_PACKAGE` and swaps the packman-managed junction at
`_build/target-deps/cae_openusd_plugins/` to point at your local extract.
The variable **must be a real environment variable in the process that runs
`repo.bat`**.

> **PowerShell trap:** `set NAME=value` in PowerShell creates a variable
> literally named `NAME=value` — it does **not** set an environment variable.
> Use `$env:NAME = "value"` in PowerShell, or use `cmd.exe` for the classic
> `set` syntax.

From cmd.exe:

```bat
set KIT_CAE_OPENUSD_PLUGINS_PACKAGE=C:\Users\<you>\Documents\OV-Composer\cae-openusd-plugins\build\packages\cae_openusd_plugins@<version>.zip
repo.bat build -rx
```

From PowerShell:

```powershell
$env:KIT_CAE_OPENUSD_PLUGINS_PACKAGE = "C:\Users\<you>\Documents\OV-Composer\cae-openusd-plugins\build\packages\cae_openusd_plugins@<version>.zip"
.\repo.bat build -rx
```

Always use a **clean build** (`-rx`) when the package override changes.

### A5. Verify the override took effect

`use_local_dependencies.py` should print a `[local-deps] Using …` line during
`repo.bat build`. After the build:

```powershell
(Get-Item _build\target-deps\cae_openusd_plugins).Target
```

- Path starting with `_build\local-deps\…` → local override active.
- Path starting with `C:\packman-repo\chk\…` → published baseline (override
  didn't apply).

Inside a running kit-cae session, from **Window → Script Editor**:

```python
from pxr import Plug, Tf
p = Plug.Registry().GetPluginWithName("omniSciVtkHdfFileFormat")
print("registered:", p is not None)
if p:
    print("resource path:", p.resourcePath)
    print("using LOCAL override:", "local-deps" in p.resourcePath.lower())
print("VtkHdfAPI:", not Tf.Type.FindByName("OmniSciFileFormatArgsVtkHdfAPI").isUnknown)
```

### A6. Restore the published dependency

```bat
set KIT_CAE_OPENUSD_PLUGINS_PACKAGE=
repo.bat build -rx
```

---

## Recipe B — usd-core / wheel build

Use this recipe when producing a wheel for pip install or a package for
standalone Python consumers linked against usd-core. It is **not** suitable
for kit-cae.

### B1. Superbuild

```powershell
cmake "-DCAE_USD_FLAVOR=usd-core" "-DCAE_USD_VERSION=25.11" -P cmake/ci/superbuild.cmake
```

First run takes 20–40 minutes; both format deps and the usd-core compile SDK
shim are prepared under `_build/sdk/` and `_build/sdk_usd/`.

### B2. Configure

```powershell
cmake -S . -B build -G Ninja `
  -C _build/sdk/cae-format-sdk-cache.cmake `
  -C _build/sdk_usd/cae-usd-sdk-cache.cmake `
  -DCMAKE_BUILD_TYPE=Release
```

### B3. Build and package

```powershell
$env:PATH = "$PWD\_build\sdk\bin;" +
            "C:\Users\<you>\AppData\Local\miniforge3\Lib\site-packages\pxr;" +
            $env:PATH
cmake --build build --parallel
cmake --build build --target package
```

Output filename:

```
build\packages\cae_openusd_plugins@0.1.1+usd-0.25.11.py312.windows-x86_64.<rev>.zip
```

---

## Repeating the build after code changes

Only the environment prep, the plugin `cmake --build`, and `--target package`
need to run again:

```powershell
if (Test-Path Env:PYTHONPATH) { Remove-Item Env:PYTHONPATH }

# Recipe A extras: keep the kit-cae USD SDK cache in place
$env:PATH = "$PWD\_build\sdk\bin;" + $env:PATH

cmake --build build --parallel
cmake --build build --target install
cmake --build build --target package
```

For kit-cae Recipe A also re-run `use_local_dependencies.py` (or just
re-launch a shell with `KIT_CAE_OPENUSD_PLUGINS_PACKAGE` set and rebuild
kit-cae) so the new zip is unpacked into `_build/local-deps/…`.

---

## Testing the vtkhdf importer

19 pytest cases exercise the plugin against the JP_VTKHDF sample data in
this repository. Under Recipe A they run against kit-cae's Python + USD:

```powershell
$kitpy   = 'C:\Users\<you>\Documents\OV-Composer\kit-cae-3.0\_build\target-deps\python\python.exe'
$usdroot = 'C:\Users\<you>\Documents\OV-Composer\kit-cae-3.0\_build\target-deps\usd\release'
$pkg     = 'C:\Users\<you>\Documents\OV-Composer\kit-cae-3.0\_build\target-deps\cae_openusd_plugins'

$env:PYTHONPATH               = "$usdroot\lib\python;$pkg\lib\python"
$env:PATH                     = "$usdroot\bin;$usdroot\lib;$pkg\plugin\usd;" + $env:PATH
$env:CAE_VTKHDF_PACKAGE_ROOT  = $pkg
$env:CAE_VTKHDF_TEST_DATA_DIR = "$PWD\JP_VTKHDF"

& $kitpy -m pip install pytest --quiet
& $kitpy -m pytest tests\python\file_format_vtkhdf -m integration -v
```

Expected: `19 passed`.
