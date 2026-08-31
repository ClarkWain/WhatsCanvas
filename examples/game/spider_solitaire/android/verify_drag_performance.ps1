param(
    [int]$DurationMs = 5000,
    [double]$MinimumFps = 45.0,
    [double]$MaximumFrameMs = 250.0,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$moduleDir = $PSScriptRoot
$androidDir = Split-Path -Parent $moduleDir
$apkPath = Join-Path $moduleDir 'build\outputs\apk\debug\spider-debug.apk'
$packageName = 'com.whatscanvas.spider'
$activityName = 'com.whatscanvas.spider/.MainActivity'

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw 'adb was not found on PATH.'
}

$devices = @(adb devices | Select-String "`tdevice$")
if ($devices.Count -ne 1) {
    throw "Expected exactly one connected Android device, found $($devices.Count)."
}

if (-not $SkipBuild) {
    & (Join-Path $androidDir 'gradlew.bat') :spider:assembleDebug
    if ($LASTEXITCODE -ne 0) { throw 'Gradle build failed.' }
}
if (-not (Test-Path -LiteralPath $apkPath)) {
    throw "APK does not exist: $apkPath"
}

adb install -r $apkPath | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'APK installation failed.' }
adb shell am force-stop $packageName
adb logcat -c
adb shell am start -W -n $activityName | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Activity launch failed.' }

# Wait until shader compilation and initial retained-picture population are
# outside the measurement window. The gesture itself always starts on the
# first tableau column's face-up card and ends in open table space, so it
# exercises selection, continuous drag rendering, and invalid-drop snap-back.
Start-Sleep -Seconds 4
$sizeLine = adb shell wm size | Select-String 'Physical size:' | Select-Object -First 1
if (-not $sizeLine -or $sizeLine.Line -notmatch '(\d+)x(\d+)') {
    throw 'Could not determine the physical display size.'
}
$panelWidth = [int]$Matches[1]
$panelHeight = [int]$Matches[2]
$landscapeWidth = [Math]::Max($panelWidth, $panelHeight)
$landscapeHeight = [Math]::Min($panelWidth, $panelHeight)
$startX = [int]($landscapeWidth * 0.185)
$startY = [int]($landscapeHeight * 0.35)
$endX = [int]($landscapeWidth * 0.74)
$endY = [int]($landscapeHeight * 0.64)

adb logcat -c
adb shell input swipe $startX $startY $endX $endY $DurationMs
Start-Sleep -Seconds 1
$log = adb logcat -d -s 'SpiderSolitaire:I' '*:S'
$sample = $log | Select-String 'DRAG_PERF' | Select-Object -Last 1
if (-not $sample) {
    throw 'No DRAG_PERF sample was emitted; the start point may not have selected a card.'
}

$pattern = 'durationMs=(\d+) frames=(\d+) moves=(\d+) avgCpuUs=(\d+) maxCpuUs=(\d+) slowFrames=(\d+)'
if ($sample.Line -notmatch $pattern) {
    throw "Malformed DRAG_PERF sample: $($sample.Line)"
}
$measuredDurationMs = [double]$Matches[1]
$frames = [int]$Matches[2]
$moves = [int]$Matches[3]
$averageFrameMs = [double]$Matches[4] / 1000.0
$maximumMeasuredFrameMs = [double]$Matches[5] / 1000.0
$slowFrames = [int]$Matches[6]
$fps = $frames * 1000.0 / $measuredDurationMs

$result = [pscustomobject]@{
    Fps = [Math]::Round($fps, 1)
    AverageFrameMs = [Math]::Round($averageFrameMs, 2)
    MaximumFrameMs = [Math]::Round($maximumMeasuredFrameMs, 2)
    Frames = $frames
    MoveEvents = $moves
    SlowFrames = $slowFrames
}
$result | Format-List | Out-Host

if ($fps -lt $MinimumFps) {
    throw "Drag FPS $([Math]::Round($fps, 1)) is below the $MinimumFps FPS requirement."
}
if ($maximumMeasuredFrameMs -gt $MaximumFrameMs) {
    throw "Maximum frame $([Math]::Round($maximumMeasuredFrameMs, 2)) ms exceeds the $MaximumFrameMs ms requirement."
}

Write-Host 'PASS: drag performance is within the configured limits.' -ForegroundColor Green
