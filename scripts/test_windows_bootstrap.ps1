param(
    [string]$BuildDirectory = "out/build/windows-msvc",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-Probe {
    param([string]$Bundle, [string]$Data, [string[]]$Extra = @())
    & $probe $Bundle $Data @Extra
    if ($LASTEXITCODE -ne 0) { throw "Bootstrap probe failed with $LASTEXITCODE" }
}

function Write-Bundle {
    param([string]$Bundle, [string]$Version, [string]$Payload)
    $source = Join-Path $scratch "source-$Version"
    Remove-Item -LiteralPath $source -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Bundle -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path (Join-Path $source "assets"), $Bundle | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $source "start.lua"), "-- $Payload`n")
    [System.IO.File]::WriteAllText((Join-Path $source "assets/payload.txt"), $Payload)
    $archive = Join-Path $Bundle "AppAssets.zip"
    Compress-Archive -Path (Join-Path $source "*") -DestinationPath $archive
    $item = Get-Item -LiteralPath $archive
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    [System.IO.File]::WriteAllText(
        (Join-Path $Bundle "AppAssets.version"),
        "format=1`nversion=$Version`nsize=$($item.Length)`nsha256=$hash`n",
        [System.Text.UTF8Encoding]::new($false))
}

function Write-MaliciousBundle {
    param([string]$Bundle)
    Remove-Item -LiteralPath $Bundle -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $Bundle | Out-Null
    $archive = Join-Path $Bundle "AppAssets.zip"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::Open($archive, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $entry = $zip.CreateEntry("../escape.txt")
        $writer = [System.IO.StreamWriter]::new($entry.Open())
        try { $writer.Write("must not escape") } finally { $writer.Dispose() }
    } finally {
        $zip.Dispose()
    }
    $item = Get-Item -LiteralPath $archive
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    [System.IO.File]::WriteAllText(
        (Join-Path $Bundle "AppAssets.version"),
        "format=1`nversion=malicious`nsize=$($item.Length)`nsha256=$hash`n",
        [System.Text.UTF8Encoding]::new($false))
}

function Invoke-ExpectedFailure {
    param([string]$Bundle, [string]$Data)
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $probe $Bundle $Data 2>$null
    $result = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    if ($result -eq 0) { throw "Invalid asset bundle unexpectedly passed verification" }
}

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $repository $BuildDirectory }
if (-not $SkipBuild) { & cmake --build $build --config $Configuration --target playground-windows-bootstrap-probe; if ($LASTEXITCODE) { throw "Build failed" } }
$probe = Join-Path $build "Engine/modern/host/sdl/$Configuration/playground-windows-bootstrap-probe.exe"
if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) { throw "Bootstrap probe is missing: $probe" }

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("playground-bootstrap-" + [Guid]::NewGuid().ToString("N"))
$bundle = Join-Path $scratch "bundle"
$data = Join-Path $scratch "Unicode データ"
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
try {
    Write-Bundle $bundle "1" "first"
    Invoke-Probe $bundle $data
    if ((Get-Content -Raw (Join-Path $data "install/assets/payload.txt")) -ne "first") { throw "Fresh extraction produced wrong data" }
    [System.IO.File]::WriteAllText((Join-Path $data "external/preserved.txt"), "keep")

    Invoke-Probe $bundle $data
    if (-not (Test-Path (Join-Path $data "external/preserved.txt"))) { throw "No-op launch removed external data" }

    Write-Bundle $bundle "2" ("second-" + ("x" * 1048576))
    & $probe $bundle $data --abort-after 4096
    if ($LASTEXITCODE -ne 75) { throw "Interrupted extraction returned $LASTEXITCODE instead of 75" }
    if ((Get-Content -Raw (Join-Path $data "install/assets/payload.txt")) -ne "first") { throw "Interrupted update damaged active install" }
    Invoke-Probe $bundle $data
    if (-not (Get-Content -Raw (Join-Path $data "install/assets/payload.txt")).StartsWith("second-")) { throw "Recovery did not activate update" }
    if ((Get-Content -Raw (Join-Path $data "external/preserved.txt")) -ne "keep") { throw "Upgrade removed external data" }

    Move-Item -LiteralPath (Join-Path $data "install") -Destination (Join-Path $data "install.old")
    Invoke-Probe $bundle $data
    if (-not (Test-Path -LiteralPath (Join-Path $data "install/start.lua"))) { throw "Rollback recovery did not restore install.old" }

    Add-Content -LiteralPath (Join-Path $bundle "AppAssets.zip") -Value "corrupt"
    Invoke-ExpectedFailure $bundle (Join-Path $scratch "corrupt-data")
    $manifestPath = Join-Path $bundle "AppAssets.version"
    $upgradeManifest =
        (Get-Content -Raw -LiteralPath $manifestPath).Replace("version=2", "version=3")
    [System.IO.File]::WriteAllText(
        $manifestPath, $upgradeManifest, [System.Text.UTF8Encoding]::new($false))
    Invoke-ExpectedFailure $bundle $data
    if (-not (Get-Content -Raw (Join-Path $data "install/assets/payload.txt")).StartsWith("second-")) { throw "Corrupt bundle damaged active install" }

    $maliciousBundle = Join-Path $scratch "malicious-bundle"
    $maliciousData = Join-Path $scratch "malicious-data"
    Write-MaliciousBundle $maliciousBundle
    Invoke-ExpectedFailure $maliciousBundle $maliciousData
    if (Test-Path -LiteralPath (Join-Path $scratch "escape.txt")) { throw "Archive traversal escaped the staging directory" }

    Write-Host "Windows asset bootstrap verification passed."
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
