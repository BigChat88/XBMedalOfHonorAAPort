# Boots an ISO in xemu and optionally saves a screenshot of its window.
# Usage: .\scripts\run-xemu.ps1 xbox\probe\probe.iso [-Screenshot out.png] [-Wait 20] [-Close]
param(
    [Parameter(Mandatory = $true)][string]$Iso,
    [string]$Screenshot,
    [int]$Wait = 20,
    [switch]$Close,
    # Alternative xemu config (e.g. tools\xemu\xemu-128.toml for 128 MB)
    [string]$Config,
    # File that receives the console's COM1 output (the game log)
    [string]$SerialLog,
    # Opens xemu's GDB stub on port 1234 (xbox/mohaa/tools/gdb-sampler.py)
    [switch]$GdbStub,
    [string]$Xemu = 'D:\Retrogames\xemu\xemu.exe'
)

$isoPath = (Resolve-Path $Iso).Path
Get-Process xemu -ErrorAction SilentlyContinue | Stop-Process -Force
$xemuArgs = @('-dvd_path', "`"$isoPath`"")
if ($Config) { $xemuArgs = @('-config_path', "`"$((Resolve-Path $Config).Path)`"") + $xemuArgs }
if ($SerialLog) {
    Remove-Item $SerialLog -ErrorAction SilentlyContinue
    $xemuArgs += @('-device', 'lpc47m157', '-serial', "`"file:$SerialLog`"")
}
if ($GdbStub) { $xemuArgs += '-s' }
$proc = Start-Process -FilePath $Xemu -ArgumentList $xemuArgs -PassThru

if (-not $Screenshot) { return }

Start-Sleep -Seconds $Wait
& "$PSScriptRoot\shot.ps1" -ProcessName xemu -Out $Screenshot

if ($Close) { Stop-Process -Id $proc.Id -Force }
