# SPDX-License-Identifier: GPL-2.0-or-later
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lock = Get-Content "$root/upstream.lock.json" -Raw | ConvertFrom-Json
$destination = Join-Path $root 'upstream'
if (Test-Path $destination) { throw 'upstream already exists; move it aside before fetching a clean research checkout.' }
git clone --no-checkout $lock.repository $destination
if ($LASTEXITCODE -ne 0) { throw 'Upstream clone failed.' }
git -C $destination checkout --detach $lock.commit
if ($LASTEXITCODE -ne 0) { throw 'Pinned upstream checkout failed.' }
git -C $destination submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw 'Upstream submodule checkout failed.' }
