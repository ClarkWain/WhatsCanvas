[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$utf8NoBom = [Text.UTF8Encoding]::new($false)

$examples = @(
    @{ Source = 'examples/tutorials/chapter02_cards.cpp'; Doc = 'doc/tutorials/02-basic-shapes.md'; Heading = '## 2.11 综合示例：绘制一组卡片' },
    @{ Source = 'examples/tutorials/chapter03_buttons.cpp'; Doc = 'doc/tutorials/03-paint-bindepth.md'; Heading = '## 3.12 综合示例：渐变按钮组' },
    @{ Source = 'examples/tutorials/chapter04_gauge.cpp'; Doc = 'doc/tutorials/04-path-bindcurves.md'; Heading = '## 4.10 综合示例：仪表盘' },
    @{ Source = 'examples/tutorials/chapter05_flower.cpp'; Doc = 'doc/tutorials/05-state-bindtransforms.md'; Heading = '## 5.10 综合示例：旋转的花朵' },
    @{ Source = 'examples/tutorials/chapter06_gallery.cpp'; Doc = 'doc/tutorials/06-image-bindrawing.md'; Heading = '## 6.11 综合示例：图片画廊' },
    @{ Source = 'examples/tutorials/chapter07_chat.cpp'; Doc = 'doc/tutorials/07-text-bindlayout.md'; Heading = '## 7.12 综合示例：聊天气泡' },
    @{ Source = 'examples/tutorials/chapter08_filters.cpp'; Doc = 'doc/tutorials/08-layer-filters.md'; Heading = '## 8.9 综合示例：iOS 风格通知面板' }
)

$outOfSync = @()

foreach ($example in $examples) {
    $sourcePath = Join-Path $repoRoot $example.Source
    $docPath = Join-Path $repoRoot $example.Doc
    $source = [IO.File]::ReadAllText($sourcePath).TrimEnd("`r", "`n")
    $doc = [IO.File]::ReadAllText($docPath)
    $newline = if ($doc.Contains("`r`n")) { "`r`n" } else { "`n" }
    $source = $source -replace "`r?`n", $newline

    $beginMarker = "<!-- BEGIN GENERATED EXAMPLE: $($example.Source) -->"
    $endMarker = "<!-- END GENERATED EXAMPLE: $($example.Source) -->"
    $generated = @(
        $beginMarker
        '```cpp'
        $source
        '```'
        $endMarker
    ) -join $newline

    $beginIndex = $doc.IndexOf($beginMarker, [StringComparison]::Ordinal)
    if ($beginIndex -ge 0) {
        $endIndex = $doc.IndexOf($endMarker, $beginIndex, [StringComparison]::Ordinal)
        if ($endIndex -lt 0) {
            throw "Missing generated end marker in $($example.Doc)"
        }
        $replaceLength = $endIndex + $endMarker.Length - $beginIndex
        $updated = $doc.Remove($beginIndex, $replaceLength).Insert($beginIndex, $generated)
    }
    else {
        $headingIndex = $doc.IndexOf($example.Heading, [StringComparison]::Ordinal)
        if ($headingIndex -lt 0) {
            throw "Missing heading '$($example.Heading)' in $($example.Doc)"
        }
        $codeStart = $doc.IndexOf('```cpp', $headingIndex, [StringComparison]::Ordinal)
        if ($codeStart -lt 0) {
            throw "Missing C++ code block after '$($example.Heading)'"
        }
        $codeEnd = $doc.IndexOf('```', $codeStart + 6, [StringComparison]::Ordinal)
        if ($codeEnd -lt 0) {
            throw "Unclosed C++ code block in $($example.Doc)"
        }
        $updated = $doc.Remove($codeStart, $codeEnd + 3 - $codeStart).Insert($codeStart, $generated)
    }

    if ($updated -cne $doc) {
        if ($Check) {
            $outOfSync += $example.Doc
        }
        else {
            [IO.File]::WriteAllText($docPath, $updated, $utf8NoBom)
            Write-Output "updated $($example.Doc)"
        }
    }
    elseif (-not $Check) {
        Write-Output "unchanged $($example.Doc)"
    }
}

if ($outOfSync.Count -gt 0) {
    $outOfSync | ForEach-Object { Write-Error "out of sync: $_" }
    exit 1
}

if ($Check) {
    Write-Output 'tutorial example code blocks are in sync'
}
