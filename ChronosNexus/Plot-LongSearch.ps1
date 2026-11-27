param([Parameter(Mandatory)][string]$ProgressCsv,[Parameter(Mandatory)][string]$OutputSvg)
$ErrorActionPreference='Stop'
[Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::InvariantCulture
$rows=Import-Csv $ProgressCsv
if(!$rows){throw 'Progress CSV is empty.'}
$progressCases=$rows|Group-Object { $_.run -replace '-(neighborhood-greedy|neighborhood|adaptive|fixed|exact-hybrid|expanding-hybrid)$','' }
$colors='#156b89','#a64e15','#487734','#763e87'
$height=65+150*$progressCases.Count
$svg=[Collections.Generic.List[string]]::new()
$svg.Add("<svg xmlns=`"http://www.w3.org/2000/svg`" width=`"800`" height=`"$height`" font-family=`"Arial`"><rect width=`"800`" height=`"$height`" fill=`"white`"/><text x=`"30`" y=`"30`" font-size=`"20`">Validated nominal Legacy incumbent by elapsed seconds</text>")
$panel=0
foreach($case in $progressCases){
 $top=60+150*$panel
 $bottom=$top+90
 $maxT=($case.Group|%{[double]$_.seconds}|Measure-Object -Maximum).Maximum
 $range=$case.Group|%{[double]$_.legacy}|Measure-Object -Minimum -Maximum
 $span=[Math]::Max(.001,$range.Maximum-$range.Minimum)
 $label=[System.Security.SecurityElement]::Escape($case.Name)
 $svg.Add("<text x=`"30`" y=`"$($top-6)`" font-size=`"13`">$label</text>")
 $svg.Add("<path d=`"M 75 $top V $bottom H 740`" fill=`"none`" stroke=`"#cccccc`"/>")
 $svg.Add("<text x=`"30`" y=`"$top`" font-size=`"10`">$($range.Maximum.ToString('0.000'))</text><text x=`"30`" y=`"$bottom`" font-size=`"10`">$($range.Minimum.ToString('0.000'))</text><text x=`"715`" y=`"$($bottom+16)`" font-size=`"10`">$($maxT.ToString('0.0')) s</text>")
 $n=0
 foreach($line in ($case.Group|Group-Object run)){
  $segments=foreach($v in $line.Group){
   $x=[Math]::Round(75+665*[double]$v.seconds/[Math]::Max(.01,$maxT),2)
   $y=[Math]::Round($bottom-90*([double]$v.legacy-$range.Minimum)/$span,2)
   "$x,$y"
  }
  $color=$colors[$n % $colors.Count]
  $legend=[System.Security.SecurityElement]::Escape($line.Name.Substring($case.Name.Length).TrimStart('-'))
  $svg.Add("<polyline points=`"$($segments -join ' ')`" fill=`"none`" stroke=`"$color`" stroke-width=`"2`"/><text x=`"$(250+115*$n)`" y=`"$($top-6)`" fill=`"$color`" font-size=`"11`">$legend</text>")
  $n++
 }
 $panel++
}
$svg.Add('</svg>')
$svg|Set-Content -LiteralPath $OutputSvg -Encoding utf8
