# SPDX-License-Identifier: GPL-3.0-or-later
# Compile the actual Vita3K ARMv7 Dynarmic recompiler for the Xbox UWP API,
# separately from Vita3K's unrelated OpenSSL/FFmpeg/libretro dependencies.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$upstream = Join-Path $root 'upstream-core'
$source = Join-Path $upstream 'external/dynarmic'
$out = Join-Path $root 'artifacts/dynarmic-uwp'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$lock = Get-Content (Join-Path $root 'core-candidates.lock.json') -Raw | ConvertFrom-Json
if (!(Test-Path (Join-Path $source 'src/dynarmic/interface/A32/a32.h'))) {
    throw "Pinned source lacks the actual Vita3K A32 recompiler"
}
$actual = (git -C $source rev-parse HEAD).Trim()
if ($actual -ne '86458a0bd369d63ba4c2ef812cacbb6c9080c065') {
    throw "Unexpected Vita3K Dynarmic source revision: $actual"
}
& "$root/scripts/apply-onyx-dynarmic.ps1" -DynarmicSource $source
# Compile the real Dynarmic execution smoke as a Store DLL, not an EXE.
# The host app can load this signed package component and call it on Series S.
$probeSource = (Join-Path $root 'tests/VitaGuestCpuProbe.cpp').Replace('\','/')
if (!(Test-Path $probeSource)) { throw 'Guest CPU execution probe source missing' }
$probeCMake = @"
add_library(vita_guest_cpu_probe SHARED "$probeSource")
target_compile_features(vita_guest_cpu_probe PRIVATE cxx_std_20)
target_link_libraries(vita_guest_cpu_probe PRIVATE dynarmic)
set_target_properties(vita_guest_cpu_probe PROPERTIES PREFIX "" OUTPUT_NAME "vita_guest_cpu_probe")
"@
Add-Content (Join-Path $source 'CMakeLists.txt') $probeCMake

$base = Join-Path $root 'build/dynarmic-x64-uwp'
$boost = (Join-Path $upstream 'external/boost').Replace('\','/')
$cmakeArgs = @(
 '-S', $source, '-B', $base, '-G', 'Visual Studio 17 2022', '-A', 'x64',
 '-DCMAKE_SYSTEM_NAME=WindowsStore', '-DCMAKE_SYSTEM_VERSION=10.0',
 '-DCMAKE_POLICY_DEFAULT_CMP0167=OLD', "-DBoost_INCLUDE_DIR:PATH=$boost",
 '-DBoost_NO_BOOST_CMAKE=ON', '-DDYNARMIC_FRONTENDS=A32',
 '-DDYNARMIC_USE_BUNDLED_EXTERNALS=ON',
 '-DDYNARMIC_ENABLE_NO_EXECUTE_SUPPORT=ON',
 '-DDYNARMIC_TESTS=OFF', '-DBUILD_TESTING=OFF',
 '-DDYNARMIC_WARNINGS_AS_ERRORS=OFF',
 '-DDYNARMIC_USE_LLVM=OFF'
)
@(
 "Target: WindowsStore x64 Dynarmic A32 CPU",
 "Actual Vita3K Dynarmic revision: $actual",
 "Onyx JIT W^X patch applied with GitHub SHA pin.",
 "No game execution is implied by a successful static .lib."
) | Set-Content (Join-Path $out 'assessment.txt')
& cmake @cmakeArgs 2>&1 | Out-File (Join-Path $out 'configure.log')
if ($LASTEXITCODE -ne 0) {
  Get-Content (Join-Path $out 'configure.log') -Tail 95 |
    Tee-Object -FilePath (Join-Path $out 'configure-tail.txt')
  throw "Dynarmic ARMv7 UWP configure failed"
}
& cmake --build $base --config Release --target dynarmic --parallel 2 2>&1 |
  Out-File (Join-Path $out 'build.log')
if ($LASTEXITCODE -ne 0) {
  Get-Content (Join-Path $out 'build.log') -Tail 95 |
    Tee-Object -FilePath (Join-Path $out 'build-tail.txt')
  throw "Dynarmic ARMv7 UWP compilation failed"
}
$libs = @(Get-ChildItem $base -Filter 'dynarmic.lib' -Recurse -File)
if (!$libs) { throw 'Target returned success but no genuine Dynarmic static library was created' }
Copy-Item $libs[0].FullName (Join-Path $out 'dynarmic.lib')
"Compiled actual Vita3K ARM guest CPU source as a WindowsStore x64 library: $($libs[0].FullName)" |
  Add-Content (Join-Path $out 'assessment.txt')
# This DLL executes three *ARM* instructions through Dynarmic, traps SVC and
# returns R0=12. Compilation alone does not mean Xbox JIT was exercised.
& cmake --build $base --config Release --target vita_guest_cpu_probe --parallel 2 2>&1 |
  Out-File (Join-Path $out 'cpu-probe-build.log')
if ($LASTEXITCODE -ne 0) {
  Get-Content (Join-Path $out 'cpu-probe-build.log') -Tail 85 |
    Tee-Object -FilePath (Join-Path $out 'cpu-probe-build-tail.txt')
  throw 'UWP guest ARM execution probe DLL failed to compile/link'
}
$dll = @(Get-ChildItem $base -Filter 'vita_guest_cpu_probe.dll' -Recurse -File)
if (!$dll) { throw 'Dynarmic DLL target returned success but no native guest CPU test DLL was created' }
Copy-Item $dll[0].FullName (Join-Path $out 'vita_guest_cpu_probe.dll')
"Packaged test component built: $($dll[0].FullName). MUST still run on Xbox hardware." |
  Add-Content (Join-Path $out 'assessment.txt')
