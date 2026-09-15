Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W34 {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr lp);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder sb, int n);
  public delegate bool EnumProc(IntPtr h, IntPtr lp);
}
"@

$p = Get-Process SokilIDE -ErrorAction SilentlyContinue
Write-Output "IDE alive: $([bool]$p)"
if (-not $p) { exit 0 }

[W34]::EnumChildWindows($p.MainWindowHandle, { 
  param($w, $lp) 
  $t = New-Object System.Text.StringBuilder 64
  [W34]::GetWindowText($w, $t, 64) | Out-Null
  if ($t.ToString() -eq "Виконати ▶") { $script:runBtn = $w; return $false }
  return $true 
}, [IntPtr]::Zero) | Out-Null

if (-not $script:runBtn) { Write-Output "run btn not found"; exit 0 }

$l = New-Object System.IntPtr ($(64) -bor ($(22) -shl 16))
[W34]::PostMessage($script:runBtn, 0x201, [IntPtr]1, $l) | Out-Null
[W34]::PostMessage($script:runBtn, 0x202, [IntPtr]0, $l) | Out-Null
Start-Sleep -Seconds 5

$root = [System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
$out = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.ClassName -eq "RichEdit" } | Select-Object -Last 1
Write-Output "output: '$($out.Current.Name)'"