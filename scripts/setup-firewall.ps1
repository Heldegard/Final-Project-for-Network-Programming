<#
.SYNOPSIS
    Prepare a Windows machine to run LanKVM.

.DESCRIPTION
    Creates inbound firewall rules for the LanKVM control/discovery ports,
    optionally switches the current network profile to Private, and optionally
    adds a Microsoft Defender exclusion for the install folder.

    Run this from an elevated PowerShell ("Run as administrator").
    Use -Undo to remove the rules and restore the previous network category.

.PARAMETER Ports
    TCP/UDP ports used by LanKVM. Default: 24800.

.PARAMETER ExePath
    Optional full path to the main executable. When supplied, an extra
    program-scoped inbound rule is created for that binary.

.PARAMETER InterfaceAlias
    Network adapter to switch to Private, for example 'WLAN'.
    Default: every adapter that is currently on a Public profile.

.PARAMETER SetNetworkPrivate
    Switch Public network profiles to Private. Required for LAN discovery and
    inbound connections on most home Wi-Fi networks.

.PARAMETER DefenderExclusion
    Add the executable folder to Microsoft Defender exclusions.

.PARAMETER Undo
    Remove the rules created by this script and restore the previous network
    category recorded at setup time.

.EXAMPLE
    .\setup-firewall.ps1 -SetNetworkPrivate

.EXAMPLE
    .\setup-firewall.ps1 -ExePath 'E:\LanKVM\lankvm.exe' -DefenderExclusion

.EXAMPLE
    .\setup-firewall.ps1 -Undo
#>
#Requires -RunAsAdministrator
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [int[]]$Ports = @(24800),
    [string]$ExePath,
    [string]$InterfaceAlias,
    [switch]$SetNetworkPrivate,
    [switch]$DefenderExclusion,
    [switch]$Undo,
    [string]$RulePrefix = 'LanKVM',
    [string]$StateDir = (Join-Path $env:ProgramData 'LanKVM')
)

$ErrorActionPreference = 'Stop'
$stateFile = Join-Path $StateDir 'firewall-state.json'

function Remove-LanKvmRules {
    $rules = Get-NetFirewallRule -DisplayName "$RulePrefix*" -ErrorAction SilentlyContinue
    if (-not $rules) {
        Write-Host "  no rules found with prefix '$RulePrefix'"
        return
    }
    foreach ($rule in $rules) {
        if ($PSCmdlet.ShouldProcess($rule.DisplayName, 'Remove firewall rule')) {
            Remove-NetFirewallRule -Name $rule.Name -ErrorAction Stop
            Write-Host "  removed: $($rule.DisplayName)"
        }
    }
}

if ($Undo) {
    Write-Host '== LanKVM firewall undo =='
    Remove-LanKvmRules
    if (Test-Path $stateFile) {
        $state = Get-Content -Path $stateFile -Raw | ConvertFrom-Json
        if ($state.InterfaceAlias -and $state.PreviousCategory) {
            $current = Get-NetConnectionProfile -InterfaceAlias $state.InterfaceAlias -ErrorAction SilentlyContinue
            if ($current -and $current.NetworkCategory -ne $state.PreviousCategory) {
                if ($PSCmdlet.ShouldProcess($state.InterfaceAlias, "Restore network category to $($state.PreviousCategory)")) {
                    Set-NetConnectionProfile -InterfaceAlias $state.InterfaceAlias -NetworkCategory $state.PreviousCategory
                    Write-Host "  restored $($state.InterfaceAlias) to $($state.PreviousCategory)"
                }
            } else {
                Write-Host "  $($state.InterfaceAlias) already at recorded category"
            }
        }
        Remove-Item -Path $stateFile -Force
    }
    Write-Host 'Done.'
    return
}

Write-Host '== inbound firewall rules =='
foreach ($port in $Ports) {
    foreach ($proto in @('TCP', 'UDP')) {
        $display = "$RulePrefix $proto $port"
        $existing = Get-NetFirewallRule -DisplayName $display -ErrorAction SilentlyContinue
        if ($existing) {
            if ($PSCmdlet.ShouldProcess($display, 'Recreate existing rule')) {
                Remove-NetFirewallRule -Name $existing.Name -ErrorAction Stop
            }
        }
        if ($PSCmdlet.ShouldProcess($display, "Create inbound allow rule ($proto/$port, Private+Domain)")) {
            New-NetFirewallRule `
                -DisplayName $display `
                -Description 'LanKVM keyboard/mouse/clipboard channel' `
                -Direction Inbound `
                -Action Allow `
                -Protocol $proto `
                -LocalPort $port `
                -Profile Private, Domain | Out-Null
            Write-Host "  created: $display"
        }
    }
}

if ($ExePath) {
    if (-not (Test-Path -LiteralPath $ExePath)) {
        Write-Warning "ExePath not found: $ExePath (skipping program-scoped rule)"
    } else {
        $display = "$RulePrefix App"
        $existing = Get-NetFirewallRule -DisplayName $display -ErrorAction SilentlyContinue
        if ($existing) {
            if ($PSCmdlet.ShouldProcess($display, 'Recreate existing rule')) {
                Remove-NetFirewallRule -Name $existing.Name -ErrorAction Stop
            }
        }
        if ($PSCmdlet.ShouldProcess($display, 'Create program-scoped inbound rule')) {
            New-NetFirewallRule `
                -DisplayName $display `
                -Description 'LanKVM application' `
                -Direction Inbound `
                -Action Allow `
                -Program $ExePath `
                -Profile Private, Domain | Out-Null
            Write-Host "  created: $display"
        }
    }
}

if ($SetNetworkPrivate) {
    Write-Host '== network profile =='
    $targets = @(Get-NetConnectionProfile | Where-Object { $_.NetworkCategory -eq 'Public' })
    if ($InterfaceAlias) {
        $targets = @($targets | Where-Object { $_.InterfaceAlias -eq $InterfaceAlias })
    }
    if ($targets.Count -eq 0) {
        Write-Host '  nothing to change (no Public profile on the selected adapter)'
    }
    foreach ($profile in $targets) {
        if ($PSCmdlet.ShouldProcess($profile.InterfaceAlias, 'Set network category to Private')) {
            if (-not (Test-Path $StateDir)) {
                New-Item -ItemType Directory -Path $StateDir -Force | Out-Null
            }
            [pscustomobject]@{
                InterfaceAlias   = $profile.InterfaceAlias
                PreviousCategory = $profile.NetworkCategory
                ChangedAt        = (Get-Date).ToString('o')
            } | ConvertTo-Json | Set-Content -Path $stateFile -Encoding UTF8
            Set-NetConnectionProfile -InterfaceAlias $profile.InterfaceAlias -NetworkCategory Private
            Write-Host "  $($profile.InterfaceAlias): Public -> Private"
        }
    }
}

if ($DefenderExclusion) {
    Write-Host '== Defender exclusion =='
    $folders = @()
    if ($ExePath) { $folders += (Split-Path -Parent $ExePath) }
    else { $folders += (Split-Path -Parent $PSScriptRoot) }
    foreach ($folder in $folders) {
        if ($PSCmdlet.ShouldProcess($folder, 'Add Defender exclusion')) {
            try {
                Add-MpPreference -ExclusionPath $folder -ErrorAction Stop
                Write-Host "  excluded: $folder"
            } catch {
                Write-Warning "could not add exclusion for $folder : $($_.Exception.Message)"
            }
        }
    }
}

Write-Host ''
Write-Host '== current state =='
Get-NetFirewallRule -DisplayName "$RulePrefix*" -ErrorAction SilentlyContinue |
    Select-Object DisplayName, Enabled, Direction, Profile |
    Format-Table -AutoSize
Get-NetConnectionProfile |
    Select-Object InterfaceAlias, NetworkCategory |
    Format-Table -AutoSize

Write-Host 'Done. If a peer still cannot connect: check that the adapter is Private and that no third-party firewall is filtering traffic.'
