# SPDX-License-Identifier: GPL-2.0-or-later
# Experimental *real* Vita3K core integration. Intentionally exercises the
# Windows Store toolchain, not a fake shim or the normal desktop DLL target.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$upstream = Join-Path $root 'upstream-core'
$out = Join-Path $root 'artifacts/core-port'
New-Item -ItemType Directory -Path $out -Force | Out-Null
if (!(Test-Path "$upstream/CMakeLists.txt")) { throw "Pinned source checkout is missing" }

$lock = Get-Content (Join-Path $root 'core-candidates.lock.json') -Raw | ConvertFrom-Json
$actual = (git -C $upstream rev-parse HEAD).Trim()
if ($actual -ne $lock.libretro_candidate.commit) { throw "Unexpected libretro source revision: $actual" }

$notes = @(
  "Vita3K libretro UWP source compile attempt",
  "Real source: $($lock.libretro_candidate.repository)",
  "Source revision: $actual",
  "Compiler target: Windows Store / x64 (NOT normal desktop Win32)",
  "Attempting full emulator build; no gameplay claim or UWP compatibility assumed.",
  "Dependencies: Dynarmic CPU/JIT, PS Vita kernel/HLE, SDL3, shader tools, Vulkan/OpenGL, package/firmware loaders.",
  "Renderer: Mesa Dozen (dzn) is a potential Vulkan-over-D3D12 layer but is not bundled or verified for Xbox UWP.",
  "Prerequisites before gameplay: UWP core DLL links, Vulkan >=1.1 HW context using a compatible driver, UWP JIT/VM, Sony firmware installation, legal homebrew boot."
)
$notes | Set-Content -Path (Join-Path $out 'assessment.txt')
$sourceList = @(
  "CMakeLists.txt",
  "vita3k/cpu/src/cpu.cpp",
  "vita3k/renderer/src/vulkan/renderer.cpp",
  "vita3k/kernel/src/thread.cpp"
)
foreach ($path in $sourceList) {
  $present = Test-Path (Join-Path $upstream $path)
  "$path => $present" | Add-Content -Path (Join-Path $out 'assessment.txt')
}

$vcpkg = "C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
if (!(Test-Path $vcpkg)) { throw "GitHub Windows runner does not include $vcpkg" }
"Starting Windows Store CMake configure..." | Add-Content -Path (Join-Path $out 'assessment.txt')
$env:VCPKG_MAX_CONCURRENCY = "2"
$env:VCPKG_KEEP_ENV_VARS = "VCPKG_MAX_CONCURRENCY"
$cmdArgs = @(
  "-S", "$upstream",
  "-B", "$root/build/core-uwp-probe",
  "-G", "Visual Studio 17 2022",
  "-A", "x64",
  "-DCMAKE_SYSTEM_NAME=WindowsStore",
  "-DCMAKE_SYSTEM_VERSION=10.0",
  "-DCMAKE_SYSTEM_PROCESSOR=AMD64",
  "-DBUILD_LIBRETRO=ON",
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkg",
  "-DVCPKG_TARGET_TRIPLET=x64-uwp",
  "-DUSE_LTO=NEVER",
  "-DVITA3K_FORCE_SYSTEM_BOOST=ON"
)
$log = Join-Path $out 'configure.log'
& cmake @cmdArgs 2>&1 | Out-File -FilePath $log
$configurationExit = $LASTEXITCODE
"Configure exit: $configurationExit" | Add-Content -Path (Join-Path $out 'assessment.txt')
Get-Content $log -Tail 90 | Out-File -FilePath (Join-Path $out 'configure-tail.txt')
if ($configurationExit -ne 0) {
  throw "Actual Vita3K core failed to configure for UWP. See uploaded configure.log for the exact blocking dependencies."
}

"Starting genuine Vita3K libretro UWP compilation..." | Add-Content -Path (Join-Path $out 'assessment.txt')
$buildLog = Join-Path $out 'build.log'
& cmake --build "$root/build/core-uwp-probe" --config Release --target vita3k_libretro --parallel 2 2>&1 | Out-File $buildLog
$buildExit = $LASTEXITCODE
"Build exit: $buildExit" | Add-Content -Path (Join-Path $out 'assessment.txt')
Get-Content $buildLog -Tail 90 | Out-File (Join-Path $out 'build-tail.txt')
if ($buildExit -ne 0) {
  throw "Real Vita3K libretro code could not be linked for UWP. See uploaded build.log."
}
$cores = Get-ChildItem "$root/build/core-uwp-probe" -Recurse -Filter 'vita3k_libretro.dll' -File
if (!$cores) { throw 'No vita3k_libretro.dll was produced by the Windows Store build' }
foreach ($core in $cores) {
    Copy-Item $core.FullName (Join-Path $out $core.Name)
    "Produced native UWP DLL: $($core.FullName)" | Add-Content (Join-Path $out 'assessment.txt')
}
"Native UWP core DLL compiled. Xbox Vulkan and actual game execution remain to be tested." |
  Add-Content (Join-Path $out 'assessment.txt')
