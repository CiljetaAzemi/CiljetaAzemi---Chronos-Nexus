param(
    [Parameter(Mandatory)][string]$SummaryCsv,
    [Parameter(Mandatory)][string]$IncumbentsCsv,
    [Parameter(Mandatory)][string]$OutputSvg
)
$ErrorActionPreference = 'Stop'
[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture
$summary = Import-Csv $SummaryCsv
$incumbents = Import-Csv $IncumbentsCsv
$colors = @('#1e63a3','#a74912')
$svg = [Collections.Generic.List[string]]::new()
$svg.Add('<svg xmlns="http://www.w3.org/2000/svg" width="780" height="660" viewBox="0 0 780 660" font-family="Arial, sans-serif" fill="#182534">')
$svg.Add('<rect width="780" height="660" fill="white"/>')
$svg.Add('<text x="40" y="34" font-size="22" font-weight="bold">Validated incumbent quality over time</text>')
$svg.Add('<text x="40" y="57" font-size="13">Standard scenarios · legacy nominal objective · elapsed time on a log scale</text>')
$i = 0
foreach ($size in 'small','medium','large') {
    $runs = @($summary | Where-Object { $_.size -eq $size -and $_.variant -eq 'legacy' -and $_.scenario -eq 'standard' })
    $limits = switch ($size) { 'small' { 25, 2500 } 'medium' { 100, 5000 } 'large' { 35000, 45000 } }
    $series = foreach ($limit in $limits) {
        $record = $runs | Where-Object { [int]$_.budget_ms -eq $limit } | Select-Object -First 1
        $points = @($incumbents | Where-Object { $_.run -eq $record.run })
        if ($points.Count) { @{Record=$record; Points=$points} }
    }
    if (!$series) { throw "No incumbent series for $size." }
    $xLeft = 96
    $xRight = 728
    $yTop = 97 + 178 * $i
    $yBottom = $yTop + 124
    $start = ($series.Points | ForEach-Object { [double]$_.seconds } | Measure-Object -Minimum).Minimum
    $stop = ($series.Record | ForEach-Object { [double]$_.engine_elapsed_s } | Measure-Object -Maximum).Maximum
    $min = [Math]::Floor(($series.Points | ForEach-Object { [double]$_.legacy_score } | Measure-Object -Minimum).Minimum) - 1
    $max = [Math]::Ceiling(($series.Points | ForEach-Object { [double]$_.legacy_score } | Measure-Object -Maximum).Maximum) + 1
    $logStart = [Math]::Log10($start)
    $logStop = [Math]::Log10($stop)
    function X([double]$s) { [Math]::Round($xLeft + ($xRight - $xLeft) * ([Math]::Log10($s) - $logStart) / ($logStop - $logStart), 2) }
    function Y([double]$q) { [Math]::Round($yBottom - ($yBottom - $yTop) * ($q - $min) / ($max - $min), 2) }
    $titleY = $yTop - 8
    $svg.Add("<text x=`"40`" y=`"$titleY`" font-size=`"15`" font-weight=`"bold`">$size</text>")
    for ($j = 0; $j -le 4; $j++) {
        $q = $min + ($max - $min) * $j / 4
        $y = Y $q
        $svg.Add("<line x1=`"$xLeft`" x2=`"$xRight`" y1=`"$y`" y2=`"$y`" stroke=`"#e0e5ec`"/>")
        $label = $q.ToString('0.#')
        $svg.Add("<text x=`"83`" y=`"$($y+4)`" text-anchor=`"end`" font-size=`"11`">$label</text>")
        $s = [Math]::Pow(10, $logStart + ($logStop - $logStart) * $j / 4)
        $x = X $s
        $label = ($s * 1000).ToString('0.#') + ' ms'
        $svg.Add("<text x=`"$x`" y=`"$($yBottom+17)`" text-anchor=`"middle`" font-size=`"11`">$label</text>")
    }
    $j = 0
    foreach ($line in $series) {
        $path = [Collections.Generic.List[string]]::new()
        foreach ($point in $line.Points) {
            $x = X $point.seconds
            $y = Y $point.legacy_score
            if (!$path.Count) { $path.Add("M $x $y") } else { $path.Add("H $x V $y") }
        }
        $path.Add("H $(X $line.Record.engine_elapsed_s)")
        $color = $colors[$j]
        $dash = if ($j -eq 0) { 'stroke-dasharray="6 4"' } else { '' }
        $svg.Add("<path d=`"$($path -join ' ')`" fill=`"none`" stroke=`"$color`" stroke-width=`"2.5`" $dash/>")
        $label = "$($line.Record.budget_ms) ms budget"
        $x = 460 + 142 * $j
        $svg.Add("<text x=`"$x`" y=`"$titleY`" fill=`"$color`" font-size=`"12`">$label</text>")
        $j++
    }
    $i++
}
$svg.Add('<text x="40" y="645" font-size="12">Each step is a published nominal replay. Horizontal endpoints are engine return times.</text>')
$svg.Add('</svg>')
$svg | Set-Content -LiteralPath $OutputSvg -Encoding utf8
