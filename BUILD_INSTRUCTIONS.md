# cae-openusd-plugins Build Instructions (Windows, usd-core 25.11)

## Prerequisites

- Visual Studio 2022 (Community or higher) with C++ workload installed
- [miniforge3](https://github.com/conda-forge/miniforge) / Python 3.12 environment with `usd-core` installed
- Git

### Install usd-core

```powershell
pip install usd-core==25.11
```

### Enable Windows Long Path Support

Run PowerShell **as Administrator**:

```powershell
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" `
    -Name "LongPathsEnabled" -Value 1

git config --global core.longpaths true
```

> Required because the OpenUSD source tree contains file paths that exceed 260 characters.

---

## Step 1 — Set Up the Build Environment

Open a **new** PowerShell window and run the following in sequence.

### Load the VS 2022 x64 developer environment

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation
```

### Add cmake and ninja to PATH

```powershell
$env:PATH = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" +
            "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;" +
            $env:PATH
```

### Verify the environment

```powershell
cmake --version   # should report 3.28.x
ninja --version
$env:LIB -split ";" | Select-String "x64"  # should show Windows Kit x64 paths
```

---

## Step 2 — Run the Superbuild

From the repository root (`cae-openusd-plugins\`):

```powershell
cmake "-DCAE_USD_FLAVOR=usd-core" "-DCAE_USD_VERSION=25.11" -P cmake/ci/superbuild.cmake
```

> **Note:** The version value must be quoted as shown to prevent PowerShell from truncating `25.11` to `25`.

This downloads and builds all format dependencies (pugixml, LZ4, zlib, xz/liblzma, HDF5, CGNS) and prepares the usd-core compile SDK shim. On first run this takes 20–40 minutes.

On completion, two cache files are written:

```
_build/sdk/cae-format-sdk-cache.cmake      # format dep roots, EDEM/CGNS/FLASH enable flags
_build/sdk_usd/cae-usd-sdk-cache.cmake     # USD_ROOT and CMAKE_PREFIX_PATH for usd-core
```

---

## Step 3 — Configure the Main Project

```powershell
cmake -S . -B build `
  -G Ninja `
  -C _build/sdk/cae-format-sdk-cache.cmake `
  -C _build/sdk_usd/cae-usd-sdk-cache.cmake `
  -DCMAKE_BUILD_TYPE=Release
```

> `-G Ninja` is required. The default Visual Studio generator causes TBB header
> incompatibilities in Debug mode that are not present in a Ninja Release build.

---

## Step 4 — Build

Add the SDK runtime DLL directories to PATH so build-time code generators can
find `z.dll` and `usd_ms.dll`:

```powershell
$env:PATH = "$PWD\_build\sdk\bin;" +
            "C:\Users\ahobbs\AppData\Local\miniforge3\Lib\site-packages\pxr;" +
            $env:PATH

cmake --build build --parallel
```

---

## Step 5 — Package

```powershell
cmake --build build --target package
```

The ZIP archive is written to `build\packages\`, e.g.:

```
build\packages\cae_openusd_plugins@0.1.1-usdcore-25.11-cp312-windows-x86_64-<rev>.zip
```

---

## Using the Package with kit-cae

In a kit-cae-3.0 checkout (Windows Command Prompt):

```bat
set KIT_CAE_OPENUSD_PLUGINS_PACKAGE=C:\Users\ahobbs\Documents\OV-Composer\cae-openusd-plugins\build\packages\cae_openusd_plugins@<version>.zip
repo.bat build -rx
```

Use a **clean build** (`-rx`) whenever the package override is set, changed, or unset.

To return to the published dependency:

```bat
set KIT_CAE_OPENUSD_PLUGINS_PACKAGE=
repo.bat build -rx
```

---

## Repeating the Build After Code Changes

The superbuild only needs to run once. For subsequent code changes:

```powershell
# Load VS env + PATH additions (Steps 1 and 4 PATH setup)
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation
$env:PATH = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" +
            "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;" +
            "$PWD\_build\sdk\bin;" +
            "C:\Users\ahobbs\AppData\Local\miniforge3\Lib\site-packages\pxr;" +
            $env:PATH

cmake --build build --parallel
cmake --build build --target package
```
