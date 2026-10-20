param([Parameter(Mandatory=$true)][string]$OutputPath, [string]$Thumbprint = "")
# Run from an elevated PowerShell window, then show the affected candidate popup.
# This script only reads process, window, pipe, and certificate state.
$ErrorActionPreference = 'Continue'
$outputPath = $OutputPath
$outputPath = [IO.Path]::GetFullPath($outputPath)

$native = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class CoralDiagnostics {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr arg);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr arg);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder name, int capacity);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool WaitNamedPipe(string name, uint timeout);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("advapi32.dll", SetLastError=true)] public static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
  [DllImport("advapi32.dll", SetLastError=true)] public static extern bool GetTokenInformation(IntPtr token, int kind, IntPtr buffer, int size, out int needed);
  [DllImport("advapi32.dll")] public static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);
  [DllImport("advapi32.dll")] public static extern IntPtr GetSidSubAuthority(IntPtr sid, uint index);
  [DllImport("advapi32.dll", CharSet=CharSet.Unicode)] public static extern bool ConvertSidToStringSid(IntPtr sid, out IntPtr text);
  [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr memory);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
Add-Type -TypeDefinition $native

$report = [System.Collections.Generic.List[string]]::new()
function Add-Line([string]$line) { $report.Add($line) }
Add-Line "Date: $(Get-Date -Format o)"
Add-Line "Identity: $([Security.Principal.WindowsIdentity]::GetCurrent().Name)"
Add-Line "Elevated: $([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))"
$certThumbprint = $Thumbprint
foreach ($path in @('Cert:\CurrentUser\Root', 'Cert:\CurrentUser\TrustedPublisher', 'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher')) {
  $cert = Get-ChildItem $path -ErrorAction SilentlyContinue | Where-Object Thumbprint -EQ $certThumbprint
  Add-Line "Certificate ${path}: $([bool]$cert)"
}
foreach ($path in @('C:\Windows\System32\coraltsf64.dll', 'C:\Program Files\coralpinyin\0.0.0.905\coral_engine_host.exe', 'C:\Program Files\coralpinyin\0.0.0.905\coral_renderer.exe')) {
  if (Test-Path -LiteralPath $path) {
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    Add-Line "Signature ${path}: $($signature.Status) $($signature.SignerCertificate.Thumbprint)"
  }
}

$processes = Get-Process SearchHost,Taskmgr,coral_engine_host,coral_renderer,coralpinyinsvc -ErrorAction SilentlyContinue
foreach ($process in $processes) {
  $session = '?'; $path = '?'
  try { $session = $process.SessionId } catch {}
  try { $path = $process.Path } catch {}
  Add-Line "Process: $($process.ProcessName) PID=$($process.Id) Session=$session Path=$path"
  $handle = [CoralDiagnostics]::OpenProcess(0x1000, $false, [uint32]$process.Id)
  if ($handle -eq [IntPtr]::Zero) {
    Add-Line "Token: PID=$($process.Id) OpenProcessError=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    continue
  }
  $token = [IntPtr]::Zero
  if ([CoralDiagnostics]::OpenProcessToken($handle, 0x8, [ref]$token)) {
    $needed = 0
    [void][CoralDiagnostics]::GetTokenInformation($token, 25, [IntPtr]::Zero, 0, [ref]$needed)
    $buffer = [Runtime.InteropServices.Marshal]::AllocHGlobal($needed)
    try {
      if ([CoralDiagnostics]::GetTokenInformation($token, 25, $buffer, $needed, [ref]$needed)) {
        $sid = [Runtime.InteropServices.Marshal]::ReadIntPtr($buffer)
        $count = [Runtime.InteropServices.Marshal]::ReadByte([CoralDiagnostics]::GetSidSubAuthorityCount($sid))
        $integrity = [Runtime.InteropServices.Marshal]::ReadInt32([CoralDiagnostics]::GetSidSubAuthority($sid, [uint32]($count - 1)))
        Add-Line "Token: PID=$($process.Id) IntegrityRID=$integrity"
      }
    } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($buffer) }
    $flag = [Runtime.InteropServices.Marshal]::AllocHGlobal(4)
    try {
      foreach ($kind in @(29, 46)) {
        if ([CoralDiagnostics]::GetTokenInformation($token, $kind, $flag, 4, [ref]$needed)) {
          Add-Line "Token: PID=$($process.Id) Class=$kind Value=$([Runtime.InteropServices.Marshal]::ReadInt32($flag))"
        } else {
          Add-Line "Token: PID=$($process.Id) Class=$kind Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
      }
    } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($flag) }
    if ($process.ProcessName -eq 'SearchHost') {
      $needed = 0
      [void][CoralDiagnostics]::GetTokenInformation($token, 31, [IntPtr]::Zero, 0, [ref]$needed)
      $package = [Runtime.InteropServices.Marshal]::AllocHGlobal($needed)
      try {
        if ([CoralDiagnostics]::GetTokenInformation($token, 31, $package, $needed, [ref]$needed)) {
          $sid = [Runtime.InteropServices.Marshal]::ReadIntPtr($package)
          $sidText = [IntPtr]::Zero
          if ([CoralDiagnostics]::ConvertSidToStringSid($sid, [ref]$sidText)) {
            Add-Line "Token: PID=$($process.Id) PackageSID=$([Runtime.InteropServices.Marshal]::PtrToStringUni($sidText))"
            [void][CoralDiagnostics]::LocalFree($sidText)
          }
        }
      } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($package) }
      foreach ($kind in @(2, 11)) {
        $needed = 0
        [void][CoralDiagnostics]::GetTokenInformation($token, $kind, [IntPtr]::Zero, 0, [ref]$needed)
        $groups = [Runtime.InteropServices.Marshal]::AllocHGlobal($needed)
        try {
          if ([CoralDiagnostics]::GetTokenInformation($token, $kind, $groups, $needed, [ref]$needed)) {
            $count = [Runtime.InteropServices.Marshal]::ReadInt32($groups)
            Add-Line "Token: PID=$($process.Id) GroupClass=$kind Count=$count"
            for ($index = 0; $index -lt $count; $index++) {
              $entry = [IntPtr]::Add($groups, 8 + $index * 16)
              $sid = [Runtime.InteropServices.Marshal]::ReadIntPtr($entry)
              $sidText = [IntPtr]::Zero
              if ([CoralDiagnostics]::ConvertSidToStringSid($sid, [ref]$sidText)) {
                $value = [Runtime.InteropServices.Marshal]::PtrToStringUni($sidText)
                if ($value.StartsWith('S-1-15-')) { Add-Line "Token: PID=$($process.Id) GroupClass=$kind SID=$value" }
                [void][CoralDiagnostics]::LocalFree($sidText)
              }
            }
          } else { Add-Line "Token: PID=$($process.Id) GroupClass=$kind Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
        } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($groups) }
      }
    }
    [void][CoralDiagnostics]::CloseHandle($token)
  } else { Add-Line "Token: PID=$($process.Id) OpenTokenError=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
  [void][CoralDiagnostics]::CloseHandle($handle)
}
$sessionId = (Get-Process -Id $PID).SessionId
foreach ($pipe in @("\\.\pipe\CoralPinyin.Session.v4.$sessionId", "\\.\pipe\CoralPinyin.Renderer.v4.$sessionId", "\\.\pipe\CoralPinyin.Renderer.v4.$sessionId.High", '\\.\pipe\_#_coralpinyin_service_bridge_#_')) {
  $available = [CoralDiagnostics]::WaitNamedPipe($pipe, 0)
  $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
  Add-Line "Pipe: $pipe Available=$available Win32Error=$errorCode"
}

$targetPids = @($processes | Where-Object ProcessName -In @('Taskmgr', 'coral_renderer', 'SearchHost') | ForEach-Object Id)
$windows = [System.Collections.Generic.List[string]]::new()
function Foreground-Info {
  $foreground = [CoralDiagnostics]::GetForegroundWindow()
  $root = [CoralDiagnostics]::GetAncestor($foreground, 2)
  $foregroundPid = [uint32]0
  $rootPid = [uint32]0
  [void][CoralDiagnostics]::GetWindowThreadProcessId($foreground, [ref]$foregroundPid)
  [void][CoralDiagnostics]::GetWindowThreadProcessId($root, [ref]$rootPid)
  $class = [Text.StringBuilder]::new(256)
  [void][CoralDiagnostics]::GetClassName($root, $class, 256)
  $rect = [CoralDiagnostics+RECT]::new()
  [void][CoralDiagnostics]::GetWindowRect($root, [ref]$rect)
  return "Foreground: HWND=$foreground PID=$foregroundPid Root=$root RootPID=$rootPid RootClass=$class Rect=$($rect.Left),$($rect.Top),$($rect.Right),$($rect.Bottom)"
}
$script:z = 0
$callback = [CoralDiagnostics+EnumProc] {
  param($hwnd, $arg)
  $name = [Text.StringBuilder]::new(256)
  [void][CoralDiagnostics]::GetClassName($hwnd, $name, 256)
  $ownerPid = [uint32]0
  [void][CoralDiagnostics]::GetWindowThreadProcessId($hwnd, [ref]$ownerPid)
  if ($ownerPid -in $targetPids -or ($script:z -lt 30 -and [CoralDiagnostics]::IsWindowVisible($hwnd))) {
    $rect = [CoralDiagnostics+RECT]::new()
    [void][CoralDiagnostics]::GetWindowRect($hwnd, [ref]$rect)
    $owner = [CoralDiagnostics]::GetWindow($hwnd, 4)
    $windows.Add("Window: Z=$script:z PID=$ownerPid HWND=$hwnd Owner=$owner Class=$name Visible=$([CoralDiagnostics]::IsWindowVisible($hwnd)) Rect=$($rect.Left),$($rect.Top),$($rect.Right),$($rect.Bottom)")
  }
  $script:z++
  return $true
}
[void][CoralDiagnostics]::EnumWindows($callback, [IntPtr]::Zero)
Add-Line (Foreground-Info)
foreach ($line in $windows) { Add-Line $line }

Write-Host 'Capturing for 12 seconds. Focus the affected window and type pinyin now.'
$visibleSample = -1
$visibleForeground = ''
$visibleWindows = @()
for ($sample = 0; $sample -lt 48; $sample++) {
  $windows.Clear()
  $script:z = 0
  [void][CoralDiagnostics]::EnumWindows($callback, [IntPtr]::Zero)
  if ($windows | Where-Object { $_ -match 'Class=CoralPinyin.RemoteCandidate.v3 Visible=True' }) {
    $visibleSample = $sample
    $visibleForeground = Foreground-Info
    $visibleWindows = @($windows)
  }
  Start-Sleep -Milliseconds 250
}
if ($visibleSample -ge 0) {
  Add-Line "Visible candidate sample: $visibleSample"
  Add-Line $visibleForeground
  foreach ($line in $visibleWindows) { Add-Line $line }
} else { Add-Line 'No visible remote candidate window during capture.' }

Set-Content -LiteralPath $outputPath -Value $report -Encoding UTF8
Write-Host "Diagnostic report: $outputPath"
