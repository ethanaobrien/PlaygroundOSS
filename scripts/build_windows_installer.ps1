param(
    [string]$BuildDirectory = "out/build/windows-msvc",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [Parameter(Mandatory = $true)]
    [string]$AppAssetsZip,
    [string]$Version = "9.11.0",
    [string]$OutputDirectory = "out/package/windows",
    [string]$ISCCPath = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    [string]$VCRedistPath,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') {
    throw "-Version must contain three or four numeric components (for example, 9.11.0)."
}

function Resolve-RepositoryPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $repository $Path
}

function Invoke-Checked {
    param([string]$Program, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    Write-Host "+ $Program $($Arguments -join ' ')"
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Program"
    }
}

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$build = Resolve-RepositoryPath $BuildDirectory
$assets = (Resolve-Path -LiteralPath (Resolve-RepositoryPath $AppAssetsZip)).Path
$output = Resolve-RepositoryPath $OutputDirectory
$iscc = (Resolve-Path -LiteralPath $ISCCPath).Path

if (-not $VCRedistPath) {
    $installationRoots = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installationRoots += & $vswhere -products * -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
    }
    foreach ($programFilesRoot in @(${env:ProgramFiles}, ${env:ProgramFiles(x86)})) {
        if (-not $programFilesRoot) { continue }
        $visualStudioRoot = Join-Path $programFilesRoot "Microsoft Visual Studio\2022"
        if (Test-Path -LiteralPath $visualStudioRoot -PathType Container) {
            $installationRoots += Get-ChildItem -LiteralPath $visualStudioRoot -Directory |
                Select-Object -ExpandProperty FullName
        }
    }
    $candidate = $installationRoots | Sort-Object -Unique |
        ForEach-Object { Join-Path $_ "VC\Redist\MSVC" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
        ForEach-Object { Get-ChildItem -LiteralPath $_ -Directory } |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "vc_redist.x64.exe" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    if (-not $candidate) { throw "Could not locate vc_redist.x64.exe; pass -VCRedistPath." }
    $VCRedistPath = $candidate
}
$redist = (Resolve-Path -LiteralPath $VCRedistPath).Path

if (-not $SkipBuild) {
    Invoke-Checked cmake --build $build --config $Configuration --target playground-sdl-engine
}

$hostDirectory = Join-Path $build "Engine/modern/host/sdl/$Configuration"
$engine = Join-Path $hostDirectory "playground-sdl-engine.exe"
if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) {
    throw "The Windows engine executable is missing: $engine"
}

$stage = Join-Path $build "windows-installer-stage"
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage, $output | Out-Null
Copy-Item -LiteralPath $engine -Destination $stage

$dlls = Get-ChildItem -LiteralPath $hostDirectory -Filter "*.dll" -File
if ($dlls.Count -eq 0) { throw "No app-local runtime DLLs were found in $hostDirectory" }
$dlls | Copy-Item -Destination $stage
Copy-Item -LiteralPath $assets -Destination (Join-Path $stage "AppAssets.zip")
Copy-Item -LiteralPath $redist -Destination (Join-Path $stage "vc_redist.x64.exe")

$assetFile = Get-Item -LiteralPath $assets
$hash = (Get-FileHash -LiteralPath $assets -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "AppAssets.zip SHA-256: $hash"
$manifest = @(
    "format=1"
    "version=$Version"
    "size=$($assetFile.Length)"
    "sha256=$hash"
) -join "`n"
[System.IO.File]::WriteAllText(
    (Join-Path $stage "AppAssets.version"), $manifest + "`n",
    [System.Text.UTF8Encoding]::new($false))

$iss = Join-Path $stage "PlaygroundOSS-SIF.iss"
Copy-Item -LiteralPath (Join-Path $repository "installer/windows/PlaygroundOSS-SIF.iss") -Destination $iss
Invoke-Checked $iscc "/DStageDir=$stage" "/DOutputDir=$output" "/DAppVersion=$Version" $iss

$installer = Get-ChildItem -LiteralPath $output -Filter "PlaygroundOSS-SIF-Setup-*.exe" -File |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if (-not $installer) { throw "Inno Setup did not produce an installer in $output" }

$installerHash = (Get-FileHash -LiteralPath $installer.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Windows installer: $($installer.FullName)"
Write-Host "SHA-256: $installerHash"
