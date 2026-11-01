# Boot one project in RPCS3, wait for a TTY marker, keep the logs, report pass or fail.
#
# Only build/<project>.elf is bootable: ppu_rules runs sprxlinker on that copy alone,
# and the top-level ELF sits in RPCS3 printing nothing.
param(
    [Parameter(Mandatory)][string]$Project,
    [Parameter(Mandatory)][string]$Expect,
    [int]$TimeoutSeconds = 60,
    [int]$ScreenshotDelaySeconds = 2,
    [switch]$Visible,
    [switch]$Screenshot,
    # Real renderer with its window hidden at once; the game writes these frames to files.
    [int[]]$Frames,
    # Only with -Frames: an input profile name from config\input_configs\global.
    [string]$InputConfig,
    # Only with -Frames: the console display mode to emulate, such as 1920x1080.
    [string]$Resolution = '1280x720',
    # Headless only: RPCS3 writes everything the game plays to a WAV.
    [switch]$DumpAudio
)

# Default is headless: no window, no focus change, nothing drawn. The Null renderer
# accepts all RSX commands, so logic and API errors still show. -Visible is for
# picture checks only, because that window can take focus from whatever the user runs.
if ($Screenshot) { $Visible = $true }

$root = Split-Path -Parent $PSScriptRoot
$rpcs3Dir = "$env:RPCS3_DIR"
$elf = Join-Path $root "$Project\build\$Project.elf"
$ttyLog = Join-Path $rpcs3Dir 'log\TTY.log'
$mainLog = Join-Path $rpcs3Dir 'log\RPCS3.log'

# RPCS3 shows a blocking dialog on a bad path, so a missing file must stop here.
if (-not (Test-Path $elf)) { Write-Output "missing ELF: $elf"; exit 2 }

# RPCS3 overwrites its logs, and a window the user opened is not ours to kill.
if (Get-Process rpcs3 -ErrorAction SilentlyContinue) { Write-Output 'rpcs3 is already running'; exit 3 }

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    public delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out Rect rect, int size);
    public static IntPtr Largest(uint processId) {
        IntPtr best = IntPtr.Zero; long bestArea = 0;
        EnumWindows((window, parameter) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != processId || !IsWindowVisible(window)) return true;
            Rect rect; DwmGetWindowAttribute(window, 9, out rect, Marshal.SizeOf(typeof(Rect)));
            long area = (long)(rect.Right - rect.Left) * (rect.Bottom - rect.Top);
            if (area > bestArea) { bestArea = area; best = window; }
            return true;
        }, IntPtr.Zero);
        return best;
    }
}
'@

# RPCS3 keeps its logs open, and Get-Content refuses a file opened that way.
function Read-SharedText([string]$path) {
    try {
        $stream = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite, Delete')
        $reader = New-Object System.IO.StreamReader $stream
        $text = $reader.ReadToEnd()
        $reader.Dispose()
        return $text
    } catch { return '' }
}

function Save-WindowCapture([System.Diagnostics.Process]$process, [string]$path) {
    $window = [WindowCapture]::Largest([uint32]$process.Id)
    if ($window -eq [IntPtr]::Zero) { Write-Output 'screenshot: no window'; return }
    $rect = New-Object WindowCapture+Rect
    [void][WindowCapture]::DwmGetWindowAttribute($window, 9, [ref]$rect, 16)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $center = $bitmap.GetPixel([int]($width / 2), [int]($height / 2))
    Write-Output "screenshot: $path ${width}x$height center=$($center.R),$($center.G),$($center.B)"
    $graphics.Dispose(); $bitmap.Dispose()
}

$runDir = Join-Path $root ("runs\{0}-{1}" -f (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss'), $Project)
New-Item -ItemType Directory -Force $runDir | Out-Null

$bootArguments = @('--no-gui')
$hdd = Join-Path $rpcs3Dir 'dev_hdd0'
$dumpList = Join-Path $hdd 'game\BLTR00001\USRDIR\dump_frames.txt'
Remove-Item $dumpList -ErrorAction SilentlyContinue
if ($Frames) {
    $Visible = $true
    New-Item -ItemType Directory -Force (Join-Path $hdd 'tmp') | Out-Null
    Remove-Item (Join-Path $hdd 'tmp\frame_*.ppm') -ErrorAction SilentlyContinue
    Set-Content -Path $dumpList -Value ($Frames -join ' ')
    $hiddenConfig = Join-Path $runDir 'hidden.yml'
    Set-Content -Path $hiddenConfig -Value "Video:`n  Renderer: Vulkan`n  Write Color Buffers: true`n  Resolution: $Resolution`nAudio:`n  Renderer: Null`nMiscellaneous:`n  Start games in fullscreen mode: false`n"
    $bootArguments += @('--config', "`"$hiddenConfig`"")
    if ($InputConfig) { $bootArguments += @('--input-config', $InputConfig) }
}
if (-not $Visible) {
    $headlessConfig = Join-Path $runDir 'headless.yml'
    Set-Content -Path $headlessConfig -Value "Video:`n  Renderer: Null`nAudio:`n  Renderer: Null`n  Dump to file: $($DumpAudio.IsPresent.ToString().ToLower())`n"
    $bootArguments = @('--headless', '--config', "`"$headlessConfig`"")
}
# A marker left in the previous run's log must not count as this run's result.
Remove-Item $ttyLog -ErrorAction SilentlyContinue
$process = Start-Process -FilePath (Join-Path $rpcs3Dir 'rpcs3.exe') -ArgumentList ($bootArguments + "`"$elf`"") -PassThru -WindowStyle Hidden
$found = $false
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)

while ((Get-Date) -lt $deadline -and -not $process.HasExited) {
    if ($Frames) {
        $window = [WindowCapture]::Largest([uint32]$process.Id)
        if ($window -ne [IntPtr]::Zero) { [void][WindowCapture]::ShowWindow($window, 0) }
    }
    if ((Read-SharedText $ttyLog).Contains($Expect)) { $found = $true; break }
    Start-Sleep -Milliseconds 100
}

if (-not $found -and (Read-SharedText $ttyLog).Contains($Expect)) { $found = $true }

if ($Screenshot -and -not $process.HasExited) {
    Start-Sleep -Seconds $ScreenshotDelaySeconds
    Save-WindowCapture $process (Join-Path $runDir 'screen.png')
}

$exited = $process.HasExited
if (-not $exited) { Stop-Process -Id $process.Id -Force; Start-Sleep -Seconds 2 }

Copy-Item $ttyLog (Join-Path $runDir 'TTY.log') -ErrorAction SilentlyContinue
Copy-Item $mainLog (Join-Path $runDir 'RPCS3.log') -ErrorAction SilentlyContinue

Move-Item (Join-Path $hdd 'tmp\frame_*.ppm') $runDir -ErrorAction SilentlyContinue
Write-Output "run: $runDir exited=$exited found=$found"
Write-Output '--- TTY'
Get-Content (Join-Path $runDir 'TTY.log') -Tail 60 -ErrorAction SilentlyContinue
Write-Output '--- RPCS3 fatal and error lines'
Select-String -Path (Join-Path $runDir 'RPCS3.log') -Pattern '^.F |^.E ' -ErrorAction SilentlyContinue |
    Select-Object -First 25 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(240, $_.Line.Length)) }

if ($found) { exit 0 }
exit 1
