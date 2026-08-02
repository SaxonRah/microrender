param(
    [string]$Runner = ".\build_copy_run_bench_rle_stress_512.bat",
    [string]$OutFile = ".\captures\bench_rle_stress_512.avi",
    [double]$CaptureFps = 70.0,
    [string]$WindowTitleRegex = "DOSBox",
    [string]$Codec = "mjpeg",
    [int]$JpegQuality = 2,
    [int]$FinalFreezeSeconds = 2
)

$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class Win32CaptureRect {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();
}
"@

[Win32CaptureRect]::SetProcessDPIAware() | Out-Null

function Resolve-PathLocal {
    param([string]$PathText)

    if ([System.IO.Path]::IsPathRooted($PathText)) {
        return $PathText
    }

    return Join-Path (Get-Location) $PathText
}

function Quote-ProcessArg {
    param([string]$ArgText)

    if ($null -eq $ArgText) {
        return '""'
    }

    if ($ArgText -notmatch '[\s"]') {
        return $ArgText
    }

    return '"' + ($ArgText -replace '"', '\"') + '"'
}

function Find-FFmpeg {
    $candidates = @(
        ".\tools\ffmpeg.exe",
        "..\tools\ffmpeg.exe",
        ".\ffmpeg.exe",
        "ffmpeg.exe"
    )

    foreach ($candidate in $candidates) {
        try {
            $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
            if ($cmd) {
                return $cmd.Source
            }
        } catch {
        }
    }

    throw "ffmpeg.exe not found. Put ffmpeg.exe in microrender_dos\tools, repo tools\, or on PATH."
}

function Find-DosBoxProcess {
    param(
        [datetime]$AfterTime,
        [string]$TitleRegex
    )

    $candidates = @()

    foreach ($proc in Get-Process -ErrorAction SilentlyContinue) {
        try {
            $proc.Refresh()

            if ($proc.HasExited) {
                continue
            }

            if ($proc.MainWindowHandle -eq [IntPtr]::Zero) {
                continue
            }

            if ($proc.StartTime -lt $AfterTime.AddSeconds(-10)) {
                continue
            }

            $title = $proc.MainWindowTitle
            $name = $proc.ProcessName

            $isDosBoxName = $name -like "*dosbox*"
            $isDosBoxTitle = $false

            if ($title) {
                $isDosBoxTitle = ($title -match $TitleRegex)
            }

            if ($isDosBoxName -or $isDosBoxTitle) {
                $candidates += $proc
            }
        } catch {
        }
    }

    if ($candidates.Count -eq 0) {
        return $null
    }

    return $candidates |
        Sort-Object StartTime -Descending |
        Select-Object -First 1
}

function Get-WindowRectInfo {
    param([IntPtr]$Hwnd)

    $rect = New-Object Win32CaptureRect+RECT

    if (-not [Win32CaptureRect]::GetWindowRect($Hwnd, [ref]$rect)) {
        throw "GetWindowRect failed."
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top

    if ($width -le 0 -or $height -le 0) {
        throw "Invalid DOSBox window rectangle: ${width}x${height}"
    }

    return [PSCustomObject]@{
        Left   = $rect.Left
        Top    = $rect.Top
        Width  = $width
        Height = $height
    }
}

function Build-CodecArgs {
    param(
        [string]$CodecName,
        [int]$Quality
    )

    if ($CodecName -eq "raw") {
        return @(
            "-c:v", "rawvideo",
            "-pix_fmt", "bgr24"
        )
    }

    return @(
        "-c:v", "mjpeg",
        "-q:v", "$Quality"
    )
}

function Start-FFmpegCapture {
    param(
        [string]$FFmpegPath,
        [string]$OutputPath,
        [double]$Fps,
        [int]$Left,
        [int]$Top,
        [int]$Width,
        [int]$Height,
        [string]$CodecName,
        [int]$Quality
    )

    $fpsString = $Fps.ToString("0.###", [System.Globalization.CultureInfo]::InvariantCulture)
    $codecArgList = Build-CodecArgs -CodecName $CodecName -Quality $Quality

    $ffmpegArgList = @(
        "-hide_banner",
        "-loglevel", "warning",
        "-y",

        "-f", "gdigrab",
        "-draw_mouse", "0",
        "-framerate", $fpsString,
        "-offset_x", "$Left",
        "-offset_y", "$Top",
        "-video_size", "${Width}x${Height}",
        "-i", "desktop"
    ) + $codecArgList + @(
        "-r", $fpsString,
        $OutputPath
    )

    $argLine = ($ffmpegArgList | ForEach-Object { Quote-ProcessArg $_ }) -join " "

    Write-Host "Starting FFmpeg capture:"
    Write-Host "  $FFmpegPath $argLine"
    Write-Host ""

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FFmpegPath
    $psi.Arguments = $argLine
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $false
    $psi.RedirectStandardError = $false
    $psi.CreateNoWindow = $false

    $ffproc = New-Object System.Diagnostics.Process
    $ffproc.StartInfo = $psi

    [void]$ffproc.Start()

    return $ffproc
}

function Stop-FFmpegCapture {
    param(
        [System.Diagnostics.Process]$FFmpegProcess
    )

    if (-not $FFmpegProcess) {
        return
    }

    try {
        if (-not $FFmpegProcess.HasExited) {
            $FFmpegProcess.StandardInput.WriteLine("q")
            $FFmpegProcess.StandardInput.Flush()

            if (-not $FFmpegProcess.WaitForExit(5000)) {
                $FFmpegProcess.Kill()
                $FFmpegProcess.WaitForExit()
            }
        }
    } catch {
        try {
            if (-not $FFmpegProcess.HasExited) {
                $FFmpegProcess.Kill()
                $FFmpegProcess.WaitForExit()
            }
        } catch {
        }
    }
}

function Run-FFmpegBlocking {
    param(
        [string]$FFmpegPath,
        [string[]]$FFmpegArgList
    )

    $argLine = ($FFmpegArgList | ForEach-Object { Quote-ProcessArg $_ }) -join " "

    Write-Host "Running FFmpeg:"
    Write-Host "  $FFmpegPath $argLine"
    Write-Host ""

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FFmpegPath
    $psi.Arguments = $argLine
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $false
    $psi.RedirectStandardOutput = $false
    $psi.RedirectStandardError = $false
    $psi.CreateNoWindow = $false

    $ffproc = New-Object System.Diagnostics.Process
    $ffproc.StartInfo = $psi

    [void]$ffproc.Start()
    $ffproc.WaitForExit()

    return $ffproc.ExitCode
}

function Add-FinalFreeze {
    param(
        [string]$FFmpegPath,
        [string]$InputPath,
        [string]$OutputPath,
        [int]$Seconds,
        [string]$CodecName,
        [int]$Quality
    )

    if (-not (Test-Path $InputPath)) {
        throw "Input capture missing: $InputPath"
    }

    if ($Seconds -le 0) {
        Move-Item -Force $InputPath $OutputPath
        return
    }

    $codecArgList = Build-CodecArgs -CodecName $CodecName -Quality $Quality

    $ffmpegArgList = @(
        "-hide_banner",
        "-loglevel", "warning",
        "-y",
        "-i", $InputPath,
        "-vf", "tpad=stop_mode=clone:stop_duration=$Seconds"
    ) + $codecArgList + @(
        $OutputPath
    )

    Write-Host "Adding final freeze frame:"
    Write-Host "  $Seconds second(s)"
    Write-Host ""

    $exitCode = Run-FFmpegBlocking -FFmpegPath $FFmpegPath -FFmpegArgList $ffmpegArgList

    if ($exitCode -ne 0) {
        Write-Host "Final freeze pass failed. Keeping raw capture instead."
        Move-Item -Force $InputPath $OutputPath
    } else {
        Remove-Item -Force $InputPath
    }
}

$ffmpeg = Find-FFmpeg
$runnerPath = Resolve-PathLocal $Runner
$outPath = Resolve-PathLocal $OutFile
$outDir = Split-Path -Parent $outPath

if (-not (Test-Path $runnerPath)) {
    throw "Runner not found: $runnerPath"
}

if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$tmpOut = Join-Path $outDir ("capture_tmp_" + [guid]::NewGuid().ToString("N") + ".avi")

$startedAt = Get-Date

Write-Host "Launching benchmark:"
Write-Host "  $runnerPath"
Write-Host ""

$runnerProc = Start-Process `
    -FilePath $env:ComSpec `
    -ArgumentList "/c", "`"$runnerPath`"" `
    -WorkingDirectory (Get-Location) `
    -PassThru

Write-Host "Waiting for DOSBox window..."

$dosbox = $null

for ($i = 0; $i -lt 300; $i++) {
    $dosbox = Find-DosBoxProcess -AfterTime $startedAt -TitleRegex $WindowTitleRegex

    if ($dosbox) {
        $dosbox.Refresh()

        if (-not $dosbox.HasExited -and $dosbox.MainWindowHandle -ne [IntPtr]::Zero) {
            break
        }
    }

    $dosbox = $null
    Start-Sleep -Milliseconds 100
}

if (-not $dosbox) {
    throw "Could not find DOSBox window. Try changing -WindowTitleRegex."
}

$dosbox.Refresh()

if ($dosbox.HasExited -or $dosbox.MainWindowHandle -eq [IntPtr]::Zero) {
    throw "Found DOSBox process, but it does not have a valid window handle."
}

$hwnd = [IntPtr]$dosbox.MainWindowHandle

[Win32CaptureRect]::ShowWindow($hwnd, 5) | Out-Null
[Win32CaptureRect]::SetForegroundWindow($hwnd) | Out-Null

Start-Sleep -Milliseconds 250

$rect = Get-WindowRectInfo -Hwnd $hwnd

Write-Host "Recording DOSBox window rectangle:"
Write-Host "  Title : $($dosbox.MainWindowTitle)"
Write-Host "  PID   : $($dosbox.Id)"
Write-Host "  HWND  : $hwnd"
Write-Host "  Rect  : x=$($rect.Left), y=$($rect.Top), w=$($rect.Width), h=$($rect.Height)"
Write-Host "  FPS   : $CaptureFps"
Write-Host "  Codec : $Codec"
Write-Host ""

$ffproc = Start-FFmpegCapture `
    -FFmpegPath $ffmpeg `
    -OutputPath $tmpOut `
    -Fps $CaptureFps `
    -Left $rect.Left `
    -Top $rect.Top `
    -Width $rect.Width `
    -Height $rect.Height `
    -CodecName $Codec `
    -Quality $JpegQuality

try {
    while ($true) {
        try {
            $dosbox.Refresh()

            if ($dosbox.HasExited) {
                break
            }
        } catch {
            break
        }

        Start-Sleep -Milliseconds 50
    }
} finally {
    Stop-FFmpegCapture -FFmpegProcess $ffproc
}

if (-not (Test-Path $tmpOut)) {
    throw "FFmpeg did not create output file."
}

Add-FinalFreeze `
    -FFmpegPath $ffmpeg `
    -InputPath $tmpOut `
    -OutputPath $outPath `
    -Seconds $FinalFreezeSeconds `
    -CodecName $Codec `
    -Quality $JpegQuality

Write-Host ""
Write-Host "Capture written:"
Write-Host "  $outPath"