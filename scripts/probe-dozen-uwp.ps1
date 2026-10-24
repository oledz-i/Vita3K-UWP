# SPDX-License-Identifier: GPL-2.0-or-later
# Probe real Mesa dzn (Vulkan-on-D3D12) sources with UWP AppContainer/Store CRT.
# This job is research-only. A produced DLL is not considered usable on Xbox
# until DLL imports, Vulkan device features and rendering are verified there.
[CmdletBinding()]
param([string]$MesaVersion = "26.2.3")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'artifacts/dozen-port'
$work = Join-Path $root 'build/dozen-port'
$src = Join-Path $work "mesa-$MesaVersion"
$build = Join-Path $work 'uwp'
$venv = Join-Path $work 'venv'
New-Item -ItemType Directory -Path $out,$work -Force | Out-Null
$notes = Join-Path $out 'assessment.txt'
@(
  "Mesa Dozen genuine UWP build probe",
  "Mesa source version: $MesaVersion",
  "Vulkan driver: microsoft-experimental (Dozen dzn)",
  "Goal: vulkan_dzn.dll compiled with Store CRT and /APPCONTAINER",
  "No external firmware, games, or GPU DLLs are bundled in the normal APPX.",
  "A DLL alone does not prove Xbox Vulkan 1.1 hardware support or Vita GXM rendering."
) | Set-Content $notes

# Mesa's official source archive. No third-party prebuilt GPU binaries.
$tar = Join-Path $work "mesa-$MesaVersion.tar.xz"
if (!(Test-Path (Join-Path $src 'meson.build'))) {
    Invoke-WebRequest "https://archive.mesa3d.org/mesa-$MesaVersion.tar.xz" -OutFile $tar
    tar -xf $tar -C $work
    if (!(Test-Path (Join-Path $src 'meson.build'))) {
        throw "Mesa source extraction failed."
    }
}
# Meson compiler detection needs a persistent cl.exe and Windows SDK env.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools unavailable' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
$env:VSLANG = "1033"
foreach ($line in (& cmd.exe /c "`"`"$vcvars`" x64 uwp >nul && set`"")) {
    if ($line -match "^([^=]+)=(.*)$") { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}
if ($env:LIB -notmatch 'x64\\store') {
    throw 'Store CRT not active; refusing misleading desktop Dozen build'
}
$storeLib = (Join-Path $env:VCToolsInstallDir 'lib/x64/store').Replace('\\','/')
if (!(Test-Path $storeLib)) { throw 'UWP Store CRT libraries missing' }
$env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"

& python -m venv $venv
$python = Join-Path $venv 'Scripts/python.exe'
& $python -m pip install --disable-pip-version-check --quiet meson ninja mako pyyaml packaging
if ($LASTEXITCODE) { throw 'Could not install Meson build tools' }
$meson = Join-Path $venv 'Scripts/meson.exe'
$ninja = Join-Path $venv 'Scripts/ninja.exe'

$cross = Join-Path $work 'uwp-x64.ini'
$lib = $storeLib.Replace('\\','/')
$crossText = @"
[binaries]
c = 'cl'
cpp = 'cl'
ar = 'lib'
[built-in options]
c_args = ['/DMESA_UWP']
cpp_args = ['/DMESA_UWP']
c_winlibs = []
cpp_winlibs = []
c_link_args = ['/LIBPATH:$lib', 'WindowsApp.lib', '/APPCONTAINER']
cpp_link_args = ['/LIBPATH:$lib', 'WindowsApp.lib', '/APPCONTAINER']
[properties]
needs_exe_wrapper = true
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
"@
Set-Content -Path $cross -Encoding ascii -Value $crossText
$args = @(
    'setup', $build, $src, '--cross-file', $cross, '--backend=ninja',
    '--buildtype=release', '-Db_vscrt=md', '-Dplatforms=windows',
    '-Dvulkan-drivers=microsoft-experimental',
    '-Dgallium-drivers=', '-Dopengl=false',
    '-Degl=disabled', '-Dglx=disabled', '-Dgbm=disabled',
    '-Dllvm=disabled', '-Dshader-cache=disabled',
    '-Dzlib=disabled', '-Dzstd=disabled', '-Dexpat=disabled',
    '-Dxmlconfig=disabled', '-Dvalgrind=disabled', '-Dlibunwind=disabled',
    '-Dbuild-tests=false', '-Dvideo-codecs=',
    '-Dintel-rt=disabled'
)
$configure = Join-Path $out 'meson-configure.log'
& $meson @args 2>&1 | Out-File $configure
$code = $LASTEXITCODE
"Meson configure returned: $code" | Add-Content $notes
if ($code) {
    Get-Content $configure -Tail 110 | Set-Content (Join-Path $out 'configure-tail.txt')
    throw 'Mesa Dozen WindowsStore Meson configure failed; consult collected logs.'
}
$buildLog = Join-Path $out 'ninja-build.log'
& $ninja -C $build 2>&1 | Out-File $buildLog
$code = $LASTEXITCODE
"Ninja build returned: $code" | Add-Content $notes
if ($code) {
    Get-Content $buildLog -Tail 110 | Set-Content (Join-Path $out 'build-tail.txt')
    throw 'Mesa Dozen UWP compilation failed; consult ninja-build.log'
}
$dlls = @(Get-ChildItem $build -Filter 'vulkan_dzn.dll' -Recurse -File)
if (!$dlls) { throw 'No Mesa Dozen vulkan_dzn.dll was produced.' }
foreach ($dll in $dlls) {
    Copy-Item $dll.FullName $out
    "Generated Dozen DLL: $($dll.FullName)" | Add-Content $notes
}
"Probe only: no UWP Vulkan context, ICD registration, or Vita homebrew boot verified." | Add-Content $notes
