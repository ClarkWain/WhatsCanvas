param(
    [int]$DurationMs = 5000,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$moduleDir = $PSScriptRoot
$androidDir = Split-Path -Parent $moduleDir
$apkPath = Join-Path $moduleDir 'build\outputs\apk\debug\spider-debug.apk'
$packageName = 'com.whatscanvas.spider'
$activityName = 'com.whatscanvas.spider/.MainActivity'

if (-not $SkipBuild) {
    & (Join-Path $androidDir 'gradlew.bat') :spider:assembleDebug
    if ($LASTEXITCODE -ne 0) { throw 'Gradle build failed.' }
}
adb install -r $apkPath | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'APK installation failed.' }

$sizeLine = adb shell wm size | Select-String 'Physical size:' | Select-Object -First 1
if (-not $sizeLine -or $sizeLine.Line -notmatch '(\d+)x(\d+)') {
    throw 'Could not determine the physical display size.'
}
$landscapeWidth = [Math]::Max([int]$Matches[1], [int]$Matches[2])
$landscapeHeight = [Math]::Min([int]$Matches[1], [int]$Matches[2])
$startX = [int]($landscapeWidth * 0.185)
$startY = [int]($landscapeHeight * 0.35)
$endX = [int]($landscapeWidth * 0.74)
$endY = [int]($landscapeHeight * 0.64)

function Invoke-Drag([string]$renderer) {
    adb shell am force-stop $packageName
    adb logcat -c
    if ($renderer -eq 'NativeCanvas') {
        adb shell am start -W -n $activityName --es renderer native | Out-Null
        $tag = 'SpiderNativeCanvas:I'
        $marker = 'DRAG_PERF_NATIVE'
    } else {
        adb shell am start -W -n $activityName | Out-Null
        $tag = 'SpiderSolitaire:I'
        $marker = 'DRAG_PERF'
    }
    Start-Sleep -Seconds 4
    adb logcat -c
    adb shell input swipe $startX $startY $endX $endY $DurationMs
    Start-Sleep -Seconds 1
    $sample = adb logcat -d -s $tag '*:S' |
        Select-String $marker | Select-Object -Last 1
    if (-not $sample) { throw "$renderer did not emit a drag sample." }

    if ($renderer -eq 'NativeCanvas') {
        $pattern = 'durationMs=(\d+) frames=(\d+) moves=(\d+) avgDrawUs=(\d+) maxDrawUs=(\d+) maxIntervalUs=(\d+)'
        if ($sample.Line -notmatch $pattern) { throw "Malformed native sample: $($sample.Line)" }
        $duration = [double]$Matches[1]
        return [pscustomobject]@{
            Renderer = $renderer
            Fps = [Math]::Round([int]$Matches[2] * 1000.0 / $duration, 1)
            AverageWorkMs = [Math]::Round([int64]$Matches[4] / 1000.0, 3)
            MaximumWorkMs = [Math]::Round([int64]$Matches[5] / 1000.0, 3)
            MaximumIntervalMs = [Math]::Round([int64]$Matches[6] / 1000.0, 3)
        }
    }

    $pattern = 'durationMs=(\d+) frames=(\d+) moves=(\d+) avgCpuUs=(\d+) maxCpuUs=(\d+) slowFrames=(\d+)'
    if ($sample.Line -notmatch $pattern) { throw "Malformed WhatsCanvas sample: $($sample.Line)" }
    $duration = [double]$Matches[1]
    return [pscustomobject]@{
        Renderer = $renderer
        Fps = [Math]::Round([int]$Matches[2] * 1000.0 / $duration, 1)
        AverageWorkMs = [Math]::Round([int64]$Matches[4] / 1000.0, 3)
        MaximumWorkMs = [Math]::Round([int64]$Matches[5] / 1000.0, 3)
        MaximumIntervalMs = '-'
    }
}

$results = @(
    Invoke-Drag 'WhatsCanvas'
    Invoke-Drag 'NativeCanvas'
)
$results | Format-Table -AutoSize | Out-Host
