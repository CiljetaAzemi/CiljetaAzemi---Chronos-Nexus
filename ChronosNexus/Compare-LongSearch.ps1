param(
 [Parameter(Mandatory)][string]$Executable,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [ValidateRange(1,3600)][int]$Seconds = 5,
 [switch]$Neighborhoods,
 [string[]]$Scenarios = @('standard','scarcity','dynamic','production-cut'),
 [string[]]$Modes,
 [switch]$HeldOut,
 [switch]$Welfare,
 [ValidateRange(0.1,3600)][double]$ReportSeconds = 5
)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $Executable).Path
$root=(New-Item -ItemType Directory -Path $OutputDirectory).FullName
$metricNames='legacy','expanded','completed','failed','service','critical_ending','total_ending','unserved','transport','fuel','waste','risk','events','coverage','critical_unserved','delivered','construction','projects','essential_unserved'
$cases=@(@('small',20261228,'standard'),@('small',20261230,'scarcity'),@('small',20270103,'dynamic'),@('medium',20270105,'production-cut'))
if($HeldOut){$cases=@(@('small',20270118,'standard'),@('small',20270119,'scarcity'),@('small',20270120,'dynamic'))}
$rows=foreach($case in $cases) {
 $size,$seed,$scenario=$case
 if($scenario -notin $Scenarios){continue}
 $modes=@('baseline','fixed','adaptive')
 if($Neighborhoods){$modes+=@('neighborhood-greedy','neighborhood')}
 if($PSBoundParameters.ContainsKey('Modes')){$modes=$PSBoundParameters['Modes']}
 foreach($mode in $modes) {
  $key="$size-$seed-$scenario-$mode"
  $directory=(New-Item -ItemType Directory (Join-Path $root $key)).FullName
  $arguments=@('--benchmark',$size,"$seed",'phase3',"--scenario=$scenario", "--time-budget-ms=$($Seconds*1000)",
    '--memory-budget-mib=1024','--pruned-initial','--feasibility-aware-allocator','--reverse-aid-bounds')
  if($Welfare){$arguments+='--welfare-priority'}
  if($mode -ne 'baseline') {
    $arguments+=@("--checkpoint=$directory/incumbent.json","--search-progress=$directory/progress.csv",
      ('--report-seconds=' + $ReportSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)))
    $arguments+=switch($mode){
        'adaptive' {'--long-search'}
        'fixed' {'--long-search-fixed'}
        'neighborhood-greedy' {'--neighborhood-greedy'}
        'neighborhood' {'--neighborhood-search'}
        'exact-hybrid' {'--exact-hybrid'}
        'expanding-hybrid' {'--expanding-hybrid'}
    }
  }
  $output=& $exe @arguments
  $exit=$LASTEXITCODE
  $output|Set-Content (Join-Path $directory 'run.log')
  $status=@($output|Where-Object {$_ -like 'ANYTIME,*'}|Select-Object -Last 1)
  $outcome=@($output|Where-Object {$_ -like 'ANYTIME_OUTCOME,*'}|Select-Object -Last 1)
  $overall=@($output|Where-Object {$_ -like 'LONG_REALIZED_OVERALL,*'}|Select-Object -Last 1)
  $row=[ordered]@{size=$size;seed=$seed;scenario=$scenario;mode=$mode;budget_s=$Seconds;exit_code=$exit}
  $statFields='validated','planning_complete','simulation_complete','first_feasible_s','first_nonempty_s','evaluated','accepted','interrupted','validation_s','reserve_s','elapsed_s','peak_bytes','reason','stage','overshoot_s'
  foreach($group in @(@($statFields,$status),@($metricNames,$outcome))){
   $names,$lines=$group
   $values=if($lines.Count){$lines[0].Split(',')}else{@()}
   for($i=0;$i -lt $names.Count;$i++){$row[$names[$i]]=if($values.Count -gt $i+1){$values[$i+1]}else{'NA'}}
  }
  $row.overall=if($overall.Count){$overall[0].Split(',')[1]}else{'NA'}
  $history=Join-Path $directory 'progress.csv'
  $curve=@(if(Test-Path $history){Import-Csv $history})
  $row.long_iterations=if($curve.Count){$curve[-1].iterations}else{'NA'}
  $row.long_improvements=if($curve.Count){$curve[-1].improvements}else{'NA'}
  $row.initial_forecast_legacy=if($curve.Count){$curve[0].legacy}else{'NA'}
  $row.best_forecast_legacy=if($curve.Count){$curve[-1].legacy}else{'NA'}
  $row.best_found_seconds=if($curve.Count){$curve[-1].best_found_seconds}else{'NA'}
  $row.exact_status=($output | Where-Object {$_ -like 'EXACT_STATUS,*'} | Select-Object -Last 1)
  $row.expanding_status=($output | Where-Object {$_ -like 'EXPANDING_MASTER,*'} | Select-Object -Last 1)
  if($row.expanding_status){
    $master=$output|Where-Object {$_ -like 'EXPANDING_MASTER,*'}|ForEach-Object {
      $v=$_.Split(',')
      [pscustomobject]@{seconds=$v[1];columns=$v[2];pool_key_bytes=$v[3];next_column=$v[4];partner=$v[5];replays=$v[6];improvements=$v[7];active_score=$v[8];backlog=([int]$v[2]-[int]$v[4]);generated=$v[9];repeated=$v[10];capacity_rejected=$v[11];compatibility_rejected=$v[12];throttled=$v[13]}
    }
    $master|Export-Csv -NoTypeInformation (Join-Path $directory 'master.csv')
  }
  [pscustomobject]$row
 }
}
$rows|Export-Csv -NoTypeInformation (Join-Path $root 'summary.csv')
