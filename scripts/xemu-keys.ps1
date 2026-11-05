# Sends key presses to the xemu window (keyboard bound to controller port 1:
# arrows = D-pad, a/b/x/y = buttons, enter = Start, backspace = Back,
# i/j/k/l = left stick, t/f/g/h = right stick, q/e = left/right trigger,
# white/black = 1/2, lclick/rclick = 3/4;
# see tools/xemu/xemu-128.toml).
# Usage: .\scripts\xemu-keys.ps1 right right a  [-HoldMs 120] [-GapMs 400]
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)][string[]]$Keys,
    [int]$HoldMs = 120,
    [int]$GapMs = 400
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class XemuKeys {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
'@

# name -> (scancode, extended)
$map = @{
    'up' = @(0x48, $true); 'down' = @(0x50, $true); 'left' = @(0x4B, $true); 'right' = @(0x4D, $true)
    'a' = @(0x1E, $false); 'b' = @(0x30, $false); 'x' = @(0x2D, $false); 'y' = @(0x15, $false)
    'enter' = @(0x1C, $false); 'backspace' = @(0x0E, $false); 'esc' = @(0x01, $false)
    'i' = @(0x17, $false); 'j' = @(0x24, $false); 'k' = @(0x25, $false); 'l' = @(0x26, $false)
    't' = @(0x14, $false); 'f' = @(0x21, $false); 'g' = @(0x22, $false); 'h' = @(0x23, $false)
    'q' = @(0x10, $false); 'e' = @(0x12, $false)
    'white' = @(0x02, $false); 'black' = @(0x03, $false); 'lclick' = @(0x04, $false); 'rclick' = @(0x05, $false)
}

$KEYEVENTF_EXTENDEDKEY = 0x1
$KEYEVENTF_KEYUP = 0x2
$KEYEVENTF_SCANCODE = 0x8

$proc = Get-Process xemu -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
[XemuKeys]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 300

foreach ($k in $Keys) {
    $entry = $map[$k.ToLower()]
    if (-not $entry) { throw "Unknown key '$k'" }
    $flags = $KEYEVENTF_SCANCODE
    if ($entry[1]) { $flags = $flags -bor $KEYEVENTF_EXTENDEDKEY }
    [XemuKeys]::keybd_event(0, [byte]$entry[0], $flags, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMs
    [XemuKeys]::keybd_event(0, [byte]$entry[0], ($flags -bor $KEYEVENTF_KEYUP), [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $GapMs
}
