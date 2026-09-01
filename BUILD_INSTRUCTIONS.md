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

Choose the USD flavor based on how the package will be consumed:

| Consumer                 | `CAE_USD_FLAVOR` | Produced USD DLL layout                             |
| ------------------------ | ---------------- | --------------------------------------------------- |
| **kit-cae 3.x / Kit SDK** | `openusd`        | Split libs (`usd_usd.dll`, `usd_sdf.dll`, ...) matching kit's `omni.usd.libs`. |
| Standalone / pip / usd-core | `usd-core`   | Monolithic `usd_ms.dll`. **Not compatible with kit-cae** (`LoadLibrary` will fail with `The specified module could not be found`). |

From the repository root (`cae-openusd-plugins\`):

```powershell
# For kit-cae consumption (this is what you want if using KIT_CAE_OPENUSD_PLUGINS_PACKAGE)
cmake "-DCAE_USD_FLAVOR=openusd" "-DCAE_USD_VERSION=25.11" -P cmake/ci/superbuild.cmake
```

> **Note:** The version value must be quoted as shown to prevent PowerShell from truncating `25.11` to `25`.

The `openusd` flavor downloads OpenUSD source and builds it with `build_usd.py`.
The `usd-core` flavor downloads a prebuilt monolithic wheel from PyPI. On first run either takes 20–40 minutes.

On completion, two cache files are written:

```
_build/sdk/cae-format-sdk-cache.cmake      # format dep roots, EDEM/CGNS/FLASH enable flags
_build/sdk_usd/cae-usd-sdk-cache.cmake     # USD_ROOT and CMAKE_PREFIX_PATH for the chosen USD
```

---

## Step 3 — Configure the Main Project

```powershell
cmake -S . -B build `
  -G Ninja `
  -C _build/sdk/cae-format-sdk-cache.cmake `
  -C _build/sdk_usd/cae-usd-sdk-cache.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  -DCAE_PACKAGE_VARIANT=openusd
```

> `-G Ninja` is required. The default Visual Studio generator causes TBB header
> incompatibilities in Debug mode that are not present in a Ninja Release build.

> `-DCAE_PACKAGE_VARIANT=openusd` stamps the produced package with the same
> variant metadata as the published `cae_openusd_plugins` artifact. Without it,
> `kit-cae-3.0/tools/use_local_dependencies.py` rejects the override with
> `Local cae_openusd_plugins package is incompatible: CAE_PACKAGE_VARIANT:
> local='', expected='openusd'`.

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

Kit-cae's `tools/use_local_dependencies.py` reads
`KIT_CAE_OPENUSD_PLUGINS_PACKAGE` and swaps the packman-managed junction at
`_build/target-deps/cae_openusd_plugins/` to point at your local extract. The
variable **must be a real environment variable in the process that runs
`repo.bat`**.

> **PowerShell trap:** `set NAME=value` in PowerShell creates a PowerShell
> variable named `NAME=value` (with `=value` as part of the name) — it does
> **not** set an environment variable. Use `$env:NAME = "value"` in PowerShell,
> or use `cmd.exe` for the `set` syntax below.

### From `cmd.exe`

```bat
set KIT_CAE_OPENUSD_PLUGINS_PACKAGE=C:\Users\ahobbs\Documents\OV-Composer\cae-openusd-plugins\build\packages\cae_openusd_plugins@<version>.zip
repo.bat build -rx
```

### From PowerShell

```powershell
$env:KIT_CAE_OPENUSD_PLUGINS_PACKAGE = "C:\Users\ahobbs\Documents\OV-Composer\cae-openusd-plugins\build\packages\cae_openusd_plugins@<version>.zip"
.\repo.bat build -rx
```

Use a **clean build** (`-rx`) whenever the package override is set, changed, or unset.

### Verify the override took effect

`use_local_dependencies.py` should print a `[local-deps] Using …` line during
`repo.bat build`. If those lines are absent, the env var wasn't visible to the
subprocess. You can also verify after the fact:

```powershell
(Get-Item _build\target-deps\cae_openusd_plugins).Target
```

- Path starting with `_build\local-deps\…` → local override active.
- Path starting with `C:\packman-repo\chk\…` → packman baseline (override
  didn't apply).

### Restore the published dependency

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
