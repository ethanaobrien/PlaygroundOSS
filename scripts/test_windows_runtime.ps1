param(
    [string]$BuildDirectory = "out/build/windows-msvc",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$InstallRoot,
    [string]$AudioAsset,
    [string]$MoviePath,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Program,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    $resolvedProgram = $Program
    if (-not [System.IO.Path]::IsPathRooted($Program) -and
        -not $Program.Contains([System.IO.Path]::DirectorySeparatorChar) -and
        -not $Program.Contains([System.IO.Path]::AltDirectorySeparatorChar)) {
        $resolvedProgram = (Get-Command $Program -ErrorAction Stop).Source
    }

    Write-Host "+ $resolvedProgram $($Arguments -join ' ')"
    $quotedArguments = @($Arguments | Where-Object { $null -ne $_ } | ForEach-Object {
        '"' + $_.Replace('"', '\"') + '"'
    })
    $startParameters = @{
        FilePath = $resolvedProgram
        NoNewWindow = $true
        Wait = $true
        PassThru = $true
    }
    if ($quotedArguments.Count -gt 0) {
        $startParameters.ArgumentList = $quotedArguments
    }
    $process = Start-Process @startParameters
    if ($process.ExitCode -ne 0) {
        throw "Command failed with exit code $($process.ExitCode): $Program"
    }
}

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repository $BuildDirectory
}

if (-not $SkipBuild) {
    Invoke-Checked cmake --build $build --config $Configuration
}

$runtime = Join-Path $build "Engine/modern/runtime/$Configuration"
$hostDirectory = Join-Path $build "Engine/modern/host/sdl/$Configuration"
$requiredFiles = @(
    (Join-Path $runtime "playground-engine-link-probe.exe"),
    (Join-Path $runtime "playground-platform-services-probe.exe"),
    (Join-Path $runtime "playground-runtime-cookie-probe.exe"),
    (Join-Path $hostDirectory "playground-desktop-host.exe"),
    (Join-Path $hostDirectory "playground-sdl-engine.exe"),
    (Join-Path $hostDirectory "playground-windows-bootstrap-probe.exe"),
    (Join-Path $hostDirectory "SDL3.dll"),
    (Join-Path $hostDirectory "libEGL.dll"),
    (Join-Path $hostDirectory "libGLESv2.dll")
)
foreach ($file in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required Windows runtime artifact is missing: $file"
    }
}

& (Join-Path $PSScriptRoot "test_windows_bootstrap.ps1") `
    -BuildDirectory $build -Configuration $Configuration -SkipBuild

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) (
    "playground-windows-probe-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    Invoke-Checked (Join-Path $runtime "playground-engine-link-probe.exe")
    Invoke-Checked (Join-Path $hostDirectory "playground-desktop-host.exe") --headless --frames 3
    Invoke-Checked (Join-Path $runtime "playground-platform-services-probe.exe") $scratch
    Invoke-Checked python (Join-Path $PSScriptRoot "test_runtime_cookies.py") `
        (Join-Path $runtime "playground-runtime-cookie-probe.exe") `
        (Join-Path $scratch "cookies")

    if ($InstallRoot) {
        $resolvedInstallRoot =
            (Resolve-Path -LiteralPath $InstallRoot).ProviderPath
        Invoke-Checked (Join-Path $runtime "playground-stream-probe.exe") `
            $resolvedInstallRoot "asset://start.lua"

        $gameExternal = Join-Path $scratch "game-user"
        New-Item -ItemType Directory -Path $gameExternal | Out-Null
        Invoke-Checked (Join-Path $hostDirectory "playground-sdl-engine.exe") `
            --install-root $resolvedInstallRoot `
            --external-root $gameExternal `
            --frames 120

        if ($AudioAsset) {
            Invoke-Checked (Join-Path $runtime "playground-audio-probe.exe") `
                $resolvedInstallRoot $gameExternal $AudioAsset
        }
    } elseif ($AudioAsset) {
        throw "-AudioAsset requires -InstallRoot"
    }

    if ($MoviePath) {
        $resolvedMovie = (Resolve-Path -LiteralPath $MoviePath).ProviderPath
        Invoke-Checked (Join-Path $runtime "playground-movie-probe.exe") `
            $resolvedMovie
    }
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Windows runtime verification passed."
$global:LASTEXITCODE = 0
