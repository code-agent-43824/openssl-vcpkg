# Builds smoke.c against one configuration of an OpenSSL artifact with MSVC, checks that every
# binary has the target architecture and imports only Windows system DLLs, then runs the test.
#
#   ./smoke/run.ps1 -Arch x64 -Linkage static -Config Release -OpenSslRoot <unpacked artifact>
#
# -Linkage static: the artifact of <arch>-windows-static, linked into the executable.
# -Linkage dll-mt: the artifact of <arch>-windows-dll-mt; its DLLs are copied next to the
#                  executable and checked too, and applink.c is compiled in (NOTES-WINDOWS.md).

param(
    [Parameter(Mandatory)][ValidateSet('x86', 'x64', 'arm64')][string]$Arch,
    [Parameter(Mandatory)][ValidateSet('static', 'dll-mt')][string]$Linkage,
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Config,
    [Parameter(Mandatory)][string]$OpenSslRoot,
    [string]$OutDir = (Join-Path $PSScriptRoot "out\$Arch-$Linkage-$Config")
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $OpenSslRoot).Path
$prefix = if ($Config -eq 'Debug') { Join-Path $root 'debug' } else { $root }
$crt = if ($Config -eq 'Debug') { '/MTd' } else { '/MT' }
# OpenSSL's NOTES-WINDOWS.md: a static build is linked with these system libraries.
$systemLibs = 'ws2_32.lib', 'gdi32.lib', 'advapi32.lib', 'crypt32.lib', 'user32.lib'
$sources = @(Join-Path $PSScriptRoot 'smoke.c')
if ($Linkage -eq 'dll-mt') {
    $applink = Join-Path $root 'include\openssl\applink.c'
    if (-not (Test-Path $applink)) { throw "$applink is missing from the artifact" }
    $sources += $applink
}

# MSVC environment for the target, native or cross-compiling from the machine's own architecture.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio not found' }
$hostArch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'amd64' }
$targetArch = @{ x86 = 'x86'; x64 = 'amd64'; arm64 = 'arm64' }[$Arch]
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=$targetArch -host_arch=$hostArch -no_logo"

$OutDir = (New-Item -ItemType Directory -Force $OutDir).FullName
$exe = Join-Path $OutDir 'smoke.exe'
Push-Location $OutDir # object files land in the current directory
try {
    & cl /nologo /W3 $crt "/I$root\include" "/Fe$exe" @sources `
        /link "/LIBPATH:$prefix\lib" libssl.lib libcrypto.lib @systemLibs
    if ($LASTEXITCODE) { throw "cl failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$binaries = @($exe)
if ($Linkage -eq 'dll-mt') {
    $dlls = @(Get-ChildItem (Join-Path $prefix 'bin') -Filter *.dll)
    if (-not $dlls) { throw "no DLLs in $prefix\bin" }
    $dlls | Copy-Item -Destination $OutDir
    $binaries += $dlls | ForEach-Object { Join-Path $OutDir $_.Name }
}

# The machine type is checked explicitly: an arm64 machine also runs x64 code under emulation,
# and an x64 machine runs x86 code under WoW64, so a passing run alone does not prove it.
# Imports: Windows system DLLs only — no CRT; OpenSSL DLLs only between the DLL build's binaries.
$machine = @{ x86 = 'machine (x86)'; x64 = 'machine (x64)'; arm64 = 'machine (ARM64)' }[$Arch]
$crtDll = '^(vcruntime|msvcp|ucrtbase|api-ms-win-crt-|concrt)'
$opensslDll = '^(libcrypto|libssl)'
foreach ($binary in $binaries) {
    $name = Split-Path $binary -Leaf
    $headers = & dumpbin /nologo /headers $binary | Out-String
    if (-not $headers.Contains($machine)) { throw "$name is not $Arch" }
    # Bare DLL names only; the "Dump of file <path>" line also ends in .dll for a DLL.
    $imports = @(& dumpbin /nologo /dependents $binary | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '^[\w.-]+\.dll$' })
    Write-Output "${name}: $machine; imports $($imports -join ', ')"
    $forbidden = @($imports | Where-Object { $_ -match $crtDll -or ($Linkage -eq 'static' -and $_ -match $opensslDll) })
    if ($forbidden) { throw "$name imports non-system DLLs: $($forbidden -join ', ')" }
    if ($binary -eq $exe -and $Linkage -eq 'dll-mt' -and @($imports -match $opensslDll).Count -lt 2) {
        throw 'smoke.exe does not import both OpenSSL DLLs'
    }
}

& $exe
if ($LASTEXITCODE) { throw "smoke.exe failed with exit code $LASTEXITCODE" }
