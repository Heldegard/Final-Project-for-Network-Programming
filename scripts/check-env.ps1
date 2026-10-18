<#
.SYNOPSIS
    Read-only health check for LanKVM, safe to run on any machine.

.DESCRIPTION
    Reports OS version, elevation, session, toolchain, firewall state, network
    profile, port availability and Defender status. Changes nothing.
    Exit code = number of hard failures.

.PARAMETER Port
    Control/discovery port to check. Default 24800.

.PARAMETER RulePrefix
    Display-name prefix used by setup-firewall.ps1.

.PARAMETER ToolchainRoot
    MinGW-w64 root used for the development build.

.EXAMPLE
    .\check-env.ps1
#>
[CmdletBinding()]
param(
    [int]$Port = 24800,
    [string]$RulePrefix = 'LanKVM',
    [string]$ToolchainRoot = 'D:\mygcc\mingw64'
)

$ErrorActionPreference = 'Continue'
$script:fail = 0
$script:warn = 0

function Ok   ($m) { Write-Host ('  [ OK ] ' + $m) -ForegroundColor Green }
function Info ($m) { Write-Host ('  [INFO] ' + $m) -ForegroundColor Gray }
function Warn ($m) { Write-Host ('  [WARN] ' + $m) -ForegroundColor Yellow; $script:warn++ }
function Bad  ($m) { Write-Host ('  [FAIL] ' + $m) -ForegroundColor Red;    $script:fail++ }

Write-Host ''
Write-Host '=== LanKVM environment check ===' -ForegroundColor Cyan

Write-Host '-- operating system --'
$os = Get-CimInstance Win32_OperatingSystem
Info "$($os.Caption) / build $($os.BuildNumber) / $($os.OSArchitecture)"
if ([int]$os.BuildNumber -ge 17763) {
    Ok 'Windows 10 1809 or newer (required APIs available)'
} else {
    Bad 'Windows 10 1809 or newer is required'
}

Write-Host '-- process context --'
$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$sessionId = (Get-Process -Id $PID).SessionId
Info "session id = $sessionId, user interactive = $([Environment]::UserInteractive)"
if ($isAdmin) {
    Ok 'running elevated'
} else {
    Warn 'not elevated: cannot create firewall rules; input injection into elevated windows will not work'
}
if ([Environment]::UserInteractive) {
    Ok 'interactive desktop session'
} else {
    Bad 'non-interactive session: SendInput/clipboard cannot be exercised here'
}

Write-Host '-- hardware --'
$cs = Get-CimInstance Win32_ComputerSystem
$cpu = @(Get-CimInstance Win32_Processor)[0]
$ramGb = $cs.TotalPhysicalMemory / 1GB
Info ('{0:N1} GB RAM, {1} cores / {2} threads' -f $ramGb, $cpu.NumberOfCores, $cpu.NumberOfLogicalProcessors)
if ($ramGb -lt 8) { Warn 'less than 8 GB RAM: running a second VM for testing will be slow' }

Write-Host '-- toolchain (development machines only) --'
$bin = Join-Path $ToolchainRoot 'bin'
foreach ($tool in @('g++.exe', 'cmake.exe', 'ninja.exe', 'ctest.exe')) {
    $path = Join-Path $bin $tool
    if (Test-Path -LiteralPath $path) { Ok "$tool -> $path" }
    else { Warn "$tool missing under $bin" }
}
$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) { Ok "git -> $($git.Source)" } else { Warn 'git not found on PATH' }

Write-Host '-- firewall --'
$profiles = @(Get-NetFirewallProfile -ErrorAction SilentlyContinue)
if ($profiles.Count -gt 0) {
    $disabled = @($profiles | Where-Object { -not $_.Enabled })
    if ($disabled.Count -eq 0) { Ok 'firewall enabled on all profiles' }
    else { Warn ("firewall disabled on: " + (($disabled | ForEach-Object { $_.Name }) -join ', ')) }
}
$rules = @(Get-NetFirewallRule -DisplayName "$RulePrefix*" -ErrorAction SilentlyContinue)
if ($rules.Count -gt 0) {
    Ok "$($rules.Count) rule(s) found for prefix '$RulePrefix'"
} else {
    Warn "no '$RulePrefix' firewall rules: run scripts\setup-firewall.ps1 from an elevated prompt"
}

Write-Host '-- network --'
# Only look at adapters that actually carry LAN traffic; skip WSL/VMware virtual NICs.
$virtualPattern = '^(vEthernet|VMware Network Adapter|Loopback|Bluetooth)'
$netProfiles = @(Get-NetConnectionProfile -ErrorAction SilentlyContinue |
    Where-Object { $_.InterfaceAlias -notmatch $virtualPattern })
if ($netProfiles.Count -eq 0) {
    Warn 'no active physical network connection'
}
foreach ($p in $netProfiles) {
    if ($p.NetworkCategory -eq 'Public') {
        Warn "$($p.InterfaceAlias): Public - inbound traffic blocked, UDP discovery unreliable (run setup-firewall.cmd -SetNetworkPrivate)"
    } else {
        Ok "$($p.InterfaceAlias): $($p.NetworkCategory)"
    }
}

Write-Host '-- ports --'
$tcpInUse = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
$udpInUse = Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue
if ($tcpInUse -or $udpInUse) {
    Warn "port $Port is already in use (another LanKVM instance?)"
} else {
    Ok "port $Port is free for both TCP and UDP"
}

Write-Host '-- security software --'
$mp = Get-MpComputerStatus -ErrorAction SilentlyContinue
if ($mp) {
    if ($mp.RealTimeProtectionEnabled) {
        Info 'Defender real-time protection is ON (low-level hooks may be flagged; add an exclusion if needed)'
    } else {
        Info 'Defender real-time protection is OFF'
    }
} else {
    Info 'Defender status unavailable (third-party AV?)'
}

Write-Host '-- optional: virtualization --'
$vmwareExe = 'C:\Program Files (x86)\VMware\VMware Workstation\vmware.exe'
if (Test-Path -LiteralPath $vmwareExe) {
    $ver = (Get-Item -LiteralPath $vmwareExe).VersionInfo.ProductVersion
    Info "VMware Workstation $ver available"
} else {
    Info 'VMware Workstation not found at the default path'
}

Write-Host ''
if ($script:fail -eq 0 -and $script:warn -eq 0) {
    Write-Host 'RESULT: all checks passed' -ForegroundColor Green
} elseif ($script:fail -eq 0) {
    Write-Host ("RESULT: $($script:warn) warning(s), no hard failures") -ForegroundColor Yellow
} else {
    Write-Host ("RESULT: $($script:fail) failure(s), $($script:warn) warning(s)") -ForegroundColor Red
}
exit $script:fail
