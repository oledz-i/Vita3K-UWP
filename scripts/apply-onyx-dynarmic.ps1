# SPDX-License-Identifier: GPL-3.0-or-later
# Compatibility-check and port the user's OnyxEMU Dynarmic W^X/JIT patches
# to the genuine Vita3K Dynarmic fork, not to Azahar's 3DS CPU emulator.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$DynarmicSource,
    [string]$OnyxRevision = "78f2561a16fe7a65e59893f896611263f10575c5"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$reportDir = Join-Path $root "artifacts/onyx-dynarmic"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$log = Join-Path $reportDir "patch-check.txt"
if (!(Test-Path (Join-Path $DynarmicSource "src/dynarmic/backend/x64/block_of_code.cpp"))) {
    throw "This is not the real Vita3K Dynarmic source checkout."
}
$onyx = Join-Path $root "build/onyx-jit-source"
if (!(Test-Path (Join-Path $onyx ".git"))) {
    git clone --depth 25 "https://github.com/oledz-i/OnyxEMU.git" $onyx
    if ($LASTEXITCODE -ne 0) { throw "Could not fetch OnyxEMU" }
}
git -C $onyx checkout --detach --force $OnyxRevision
if ($LASTEXITCODE -ne 0) { throw "Onyx source pin cannot be checked out" }
$actual = (git -C $onyx rev-parse HEAD).Trim()
if ($actual -ne $OnyxRevision) { throw "Source revision does not match Onyx lock" }
$patchDir = Join-Path $onyx "patches/dynarmic"
$patches = @(Get-ChildItem $patchDir -Filter "*.patch" | Sort-Object Name)
if ($patches.Count -eq 0) { throw "No Onyx Dynarmic W^X patches in pinned checkout" }

@(
 "Original OnyxEMU source: $OnyxRevision",
 "Original source license: GPL-3.0-or-later",
 "Applying patches to REAL Vita3K/Dynarmic checkout:",
 "$DynarmicSource",
 "Vita3K fork revision: $((git -C $DynarmicSource rev-parse HEAD).Trim())",
 "Azahar code and shaders are not copied. Only UWP x64 JIT memory-management patches.",
 "Onyx Dynarmic base was $((Get-Content (Join-Path $patchDir 'BASE_COMMIT') -Raw).Trim())",
 "Patch count: $($patches.Count)"
) | Set-Content $log

foreach ($patch in $patches) {
    "Checking $($patch.Name)" | Add-Content $log
    $check = (& git -C $DynarmicSource apply --check --whitespace=nowarn $patch.FullName 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        "INCOMPATIBLE upstream context at $($patch.Name):" | Add-Content $log
        $check | Add-Content $log
        throw "The original Onyx Dynarmic patch $($patch.Name) requires porting to Vita3K's Dynarmic revision. See patch-check.txt"
    }
    & git -C $DynarmicSource apply --whitespace=nowarn $patch.FullName 2>&1 | Out-String |
        Add-Content $log
    if ($LASTEXITCODE -ne 0) {
        throw "Applying $($patch.Name) failed after a successful context check"
    }
    "APPLIED $($patch.Name)" | Add-Content $log
}

& git -C $DynarmicSource diff --check 2>&1 | Out-String | Add-Content $log
if ($LASTEXITCODE -ne 0) { throw "Onyx patched Vita Dynarmic has whitespace/conflict markers" }
& git -C $DynarmicSource diff --stat 2>&1 | Out-String | Add-Content $log
$code = Get-Content (Join-Path $DynarmicSource "src/dynarmic/backend/x64/block_of_code.cpp") -Raw
if ($code -notmatch "VirtualProtectFromApp|DYNARMIC_ENABLE_NO_EXECUTE_SUPPORT") {
    throw "W^X/FromApp patching not present in real Vita Dynarmic after patch series"
}
"Patched Vita3K Dynarmic source passed the original Onyx W^X patch context check; WindowsStore compilation and guest execution still unproven." | Add-Content $log
Write-Host "Real Vita3K Dynarmic patched with Onyx W^X source; see $log"
