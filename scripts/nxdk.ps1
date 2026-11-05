# Runs a command inside the nxdk container with the repo mounted at /work.
# Usage: .\scripts\nxdk.ps1 xbox/probe make -j8
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Cmd
)

$root = (Resolve-Path "$PSScriptRoot\..").Path
if (-not $Cmd) { $Cmd = @('make', '-j8') }

docker run --rm `
    -v "${root}:/work" `
    -w "/work/$Dir" `
    -e NXDK_DIR=/usr/src/nxdk `
    ghcr.io/xboxdev/nxdk:latest `
    @Cmd
exit $LASTEXITCODE
