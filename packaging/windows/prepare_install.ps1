param(
    [string]$PreviousInstallDir = '',
    [string]$PreviousProductRoot = '',
    [string]$TargetInstallDir = '',
    [switch]$ProcessesOnly
)

$ErrorActionPreference = 'Stop'
$productIdentity = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot 'product_identity.psd1')

try {
    if (-not $ProcessesOnly) {
        $service = Get-Service -Name $productIdentity.service_name -ErrorAction SilentlyContinue
        if ($null -ne $service) {
            $service.Refresh()
            if ($service.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Stopped) {
                Stop-Service -Name $productIdentity.service_name -ErrorAction Stop
                $service.WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Stopped,
                    [TimeSpan]::FromSeconds(15))
            }
        }
    }

    $targetRoot = [IO.Path]::GetFullPath($TargetInstallDir).TrimEnd('\') + '\'
    $roots = @([IO.Path]::GetDirectoryName($targetRoot.TrimEnd('\')))
    if (-not [string]::IsNullOrWhiteSpace($PreviousProductRoot)) {
        $roots += $PreviousProductRoot
    } elseif (-not [string]::IsNullOrWhiteSpace($PreviousInstallDir)) {
        $roots += $PreviousInstallDir
    }
    $roots = @($roots | ForEach-Object { [IO.Path]::GetFullPath($_).TrimEnd('\') + '\' } |
        Select-Object -Unique)
    $names = @($productIdentity.settings_exe, $productIdentity.update_exe, $productIdentity.diagnostic_exe,
        $productIdentity.service_exe, $productIdentity.session_host_exe, $productIdentity.renderer_exe)
    $filter = ($names | ForEach-Object { "Name='$_'" }) -join ' OR '

    function Get-ProductProcesses {
        Get-CimInstance Win32_Process -Filter $filter | Where-Object {
            $path = $_.ExecutablePath
            if ($null -eq $path) { return $false }
            foreach ($root in $roots) {
                if ($path.StartsWith($root,
                        [StringComparison]::OrdinalIgnoreCase)) { return $true }
            }
            return $false
        }
    }

    foreach ($process in @(Get-ProductProcesses)) {
        Write-Output "Stopping $($process.Name) (PID $($process.ProcessId))"
        $handle = $null
        try {
            $handle = [Diagnostics.Process]::GetProcessById($process.ProcessId)
            Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
            if (-not $handle.WaitForExit(10000)) {
                throw "Process $($process.ProcessId) did not exit within 10 seconds"
            }
        } catch {
            if (Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue) {
                throw
            }
            # The process exited after the snapshot.
        } finally {
            if ($null -ne $handle) { $handle.Dispose() }
        }
    }

    $remaining = @(Get-ProductProcesses)
    if ($remaining.Count -gt 0) {
        # A different version has its own directory; Windows may remove the old
        # image files after reboot. A same-version repair must not overwrite them.
        $targetRunning = @($remaining | Where-Object {
            $_.ExecutablePath.StartsWith($targetRoot,
                [StringComparison]::OrdinalIgnoreCase)
        })
        if ($ProcessesOnly -or $targetRunning.Count -gt 0) {
            throw "Old CORAL processes are still running: $(($remaining | ForEach-Object { "$($_.Name):$($_.ProcessId)" }) -join ', ')"
        }
        Write-Output 'Old version restarted; its files will be removed after reboot.'
    }
    exit 0
} catch {
    Write-Error $_
    exit 1
}
