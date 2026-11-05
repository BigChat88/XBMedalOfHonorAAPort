# Loads each map in OpenMoHAA (PC, x86), lets it settle, runs `meminfo` and
# `imagelist`, and stores the per-pool memory usage in a CSV.
# Usage: .\scripts\measure-maps.ps1 [-Maps m1l1,m3l1a] [-Wait 45]
param(
    [string[]]$Maps,
    [int]$Wait = 45,
    [string]$Out = "$PSScriptRoot\..\docs\memory-pc.csv"
)

$root = (Resolve-Path "$PSScriptRoot\..").Path
$exeDir = "$root\tools\openmohaa-pc"
$homeDir = "$exeDir\home"
$log = "$homeDir\main\qconsole.log"

if (-not $Maps) {
    $Maps = 'training', 'm1l1', 'm1l2a', 'm1l2b', 'm1l3a', 'm1l3b', 'm1l3c',
        'm2l1', 'm2l2a', 'm2l2b', 'm2l2c', 'm2l3', 'm3l1a', 'm3l1b', 'm3l2', 'm3l3',
        'm4l0', 'm4l1', 'm4l2', 'm4l3', 'm5l1a', 'm5l1b', 'm5l2a', 'm5l2b', 'm5l3',
        'm6l1a', 'm6l1b', 'm6l1c', 'm6l2a', 'm6l2b', 'm6l3a', 'm6l3b', 'm6l3c', 'm6l3d', 'm6l3e'
}

New-Item -ItemType Directory -Force "$homeDir\main", (Split-Path $Out) | Out-Null
# `wait N` waits N frames; with the map loaded the game runs at 60+ fps.
Set-Content -Encoding ascii "$homeDir\main\measure.cfg" "wait 4000`nmeminfo`nimagelist`nwait 10`nquit`n"

$rows = @()
foreach ($map in $Maps) {
    Remove-Item $log -ErrorAction SilentlyContinue
    $argv = @('+set', 'fs_basepath', "`"$root\gamedata\mohaa`"", '+set', 'fs_homepath', "`"$homeDir`"",
        '+set', 'r_fullscreen', '0', '+set', 'r_mode', '3', '+set', 'logfile', '2',
        '+set', 'com_skipIntroVideo', '1', '+set', 's_initsound', '1',
        '+map', $map, '+exec', 'measure.cfg')
    $p = Start-Process "$exeDir\openmohaa.exe" -WorkingDirectory $exeDir -ArgumentList $argv -PassThru

    $peakPriv = 0
    $deadline = (Get-Date).AddSeconds($Wait + 60)
    while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $p.Refresh()
        if (-not $p.HasExited -and $p.PrivateMemorySize64 -gt $peakPriv) { $peakPriv = $p.PrivateMemorySize64 }
    }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $status = 'timeout' } else { $status = 'ok' }
    # Launching the next instance too soon makes it die at startup.
    Start-Sleep -Seconds 3

    $row = [ordered]@{ map = $map; status = $status; process_peak_MB = [math]::Round($peakPriv / 1MB, 1) }
    $text = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
    foreach ($m in [regex]::Matches($text, '(\d+) bytes in \d+ blocks in ([\w ]+?) memory pool')) {
        $row["$($m.Groups[2].Value)_MB"] = [math]::Round([double]$m.Groups[1].Value / 1MB, 2)
    }
    # OpenMoHAA's R_CountTextureMemory has a bug (= instead of +=); use imagelist.
    $tx = [regex]::Match($text, '(\d+) total texels')
    if ($tx.Success) { $row['textures_RGBA8_MB'] = [math]::Round([double]$tx.Groups[1].Value * 4 / 1MB, 2) }
    $ti = [regex]::Match($text, '(\d+) total images')
    if ($ti.Success) { $row['images'] = [int]$ti.Groups[1].Value }
    $tot = [regex]::Match($text, '([\d.]+) Kbytes in \d+ blocks in all memory pools')
    if ($tot.Success) { $row['pools_total_MB'] = [math]::Round([double]$tot.Groups[1].Value / 1024, 2) }
    if (-not $tot.Success) { $row['status'] = 'no meminfo' }
    $row['script_errors'] = ([regex]::Matches($text, '\] \^~\^~\^|\] ERROR: ')).Count

    Copy-Item $log "$(Split-Path $Out)\logs-$map.log" -ErrorAction SilentlyContinue
    $rows += [pscustomobject]$row
    "{0,-8} {1,-8} process={2} MB pools={3} MB textures={4} MB" -f $map, $row.status, $row.process_peak_MB, $row.pools_total_MB, $row.textures_RGBA8_MB
}

# Merge the columns of every row for the CSV
$cols = $rows | ForEach-Object { $_.PSObject.Properties.Name } | Select-Object -Unique
$rows | Select-Object $cols | Export-Csv -NoTypeInformation -Encoding utf8 $Out
"CSV: $Out"
