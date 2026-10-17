# Builds smoke.c against one configuration of an OpenSSL artifact with MSVC, checks that the
# executable has the target architecture and no non-system DLL dependencies, then runs it.
#
#   ./smoke/run.ps1 -Arch x64 -Config Release -OpenSslRoot <unpacked openssl-x64-windows-static>

param(
    [Parameter(Mandatory)][ValidateSet('x86', 'x64', 'arm64')][string]$Arch,
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Config,
    [Parameter(Mandatory)][string]$OpenSslRoot,
    [string]$OutDir = (Join-Path $PSScriptRoot "out\$Arch-$Config")
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $OpenSslRoot).Path
$libDir = if ($Config -eq 'Debug') { Join-Path $root 'debug\lib' } else { Join-Path $root 'lib' }
$crt = if ($Config -eq 'Debug') { '/MTd' } else { '/MT' }
# OpenSSL's NOTES-WINDOWS.md: a static build is linked with these system libraries.
$systemLibs = 'ws2_32.lib', 'gdi32.lib', 'advapi32.lib', 'crypt32.lib', 'user32.lib'

# MSVC environment for the target, native or cross-compiling from the machine's own architecture.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio not found' }
$hostArch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'amd64' }
$targetArch = @{ x86 = 'x86'; x64 = 'amd64'; arm64 = 'arm64' }[$Arch]
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=$targetArch -host_arch=$hostArch -no_logo"

New-Item -ItemType Directory -Force $OutDir | Out-Null
$obj = Join-Path $OutDir 'smoke.obj'
$exe = Join-Path $OutDir 'smoke.exe'
& cl /nologo /W3 $crt "/I$root\include" "/Fo$obj" "/Fe$exe" (Join-Path $PSScriptRoot 'smoke.c') `
    /link "/LIBPATH:$libDir" libssl.lib libcrypto.lib @systemLibs
if ($LASTEXITCODE) { throw "cl failed with exit code $LASTEXITCODE" }

# Checked explicitly: an arm64 machine also runs x64 code under emulation, and an x64 machine
# runs x86 code under WoW64, so a passing run alone does not prove the architecture.
$machine = @{ x86 = 'machine (x86)'; x64 = 'machine (x64)'; arm64 = 'machine (ARM64)' }[$Arch]
$headers = & dumpbin /nologo /headers $exe | Out-String
if (-not $headers.Contains($machine)) { throw "smoke.exe is not $Arch" }
Write-Output "Architecture: $machine"

# Fully static: only Windows system DLLs may be imported — no CRT, no OpenSSL DLLs.
$dlls = @(& dumpbin /nologo /dependents $exe | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '\.dll$' })
Write-Output "Imports: $($dlls -join ', ')"
$forbidden = @($dlls | Where-Object { $_ -match '^(vcruntime|msvcp|ucrtbase|api-ms-win-crt-|concrt|libcrypto|libssl)' })
if ($forbidden) { throw "Non-system DLL dependencies: $($forbidden -join ', ')" }

& $exe
if ($LASTEXITCODE) { throw "smoke.exe failed with exit code $LASTEXITCODE" }
