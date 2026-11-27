param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$OutputDirectory,[switch]$CollectOnly)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path $Executable).Path
$root=if($CollectOnly){(Resolve-Path $OutputDirectory).Path}else{(New-Item -ItemType Directory $OutputDirectory).FullName}
$budgets=@{small=5000;medium=8000;large=45000;extreme=60000}
$dimensions=@{small=@(100,500,50);medium=@(1000,10000,500);large=@(10000,100000,5000);extreme=@(100000,1000000,50000)}
$exitCodes=@{}
if($CollectOnly){foreach($line in Get-Content (Join-Path $root status.txt)){$size,$mode,$code=$line.Split(',');$exitCodes["$size-$mode"]=$code}}
function Add-Record($Row,$Lines,$Name,$Fields){
    $record=$Lines|Where-Object {$_.StartsWith("$Name,")}|Select-Object -Last 1
    $values=if($record){$record.Split(',')}
    for($i=0;$i -lt $Fields.Count;++$i){$Row[$Fields[$i]]=if($values.Count -gt $i+1){$values[$i+1]}else{'NA'}}
}
$rows=foreach($size in 'small','medium','large','extreme'){
    foreach($mode in 'baseline','delivery'){
        $key="$size-$mode"
        $log=Join-Path $root "$key.log"
        $ms=$budgets[$size]
        if(!$CollectOnly){
            $arguments=@('--benchmark',$size,'20261217','phase3','--scenario=dynamic',"--time-budget-ms=$ms",
                '--memory-budget-mib=1024','--reserve-ms=4000','--trace-plan')
            if($mode -eq 'delivery'){
                $arguments+='--pruned-initial','--welfare-priority'
                if($size -eq 'small'){$arguments+='--expanding-hybrid','--feasibility-aware-allocator','--reverse-aid-bounds'}
            }
            $launch=@{FilePath=$exe;ArgumentList=$arguments;PassThru=$true;WindowStyle='Hidden';
                RedirectStandardOutput=$log;RedirectStandardError=(Join-Path $root "$key.err")}
            $process=Start-Process @launch
            $null=$process.Handle
            try{
                if($process.WaitForExit($ms+15000)){$exitCodes[$key]=$process.ExitCode}
                else{$process.Kill();$process.WaitForExit();$exitCodes[$key]='watchdog'}
            }finally{$process.Dispose()}
            "$size,$mode,$($exitCodes[$key])"|Add-Content (Join-Path $root status.txt)
        }
        $lines=Get-Content -LiteralPath $log
        $p,$r,$m=$dimensions[$size]
        $row=[ordered]@{size=$size;mode=$mode;seed=20261217;scenario='dynamic';planets=$p;routes=$r;
            external_missions=$m;budget_ms=$ms;exit_status=$exitCodes[$key]}
        Add-Record $row $lines 'ANYTIME' @('validated','planning_complete','simulation_complete','first_feasible_s',
            'first_nonempty_s','evaluated','accepted','interrupted','validation_s','reserve_s','elapsed_s','peak_bytes','reason','stage','overshoot_s')
        Add-Record $row $lines 'ANYTIME_OUTCOME' @('legacy','expanded','completed','failed','service','critical_ending',
            'total_ending','unserved','transport','fuel','waste','risk','events','coverage','critical_unserved',
            'delivered','construction','projects','essential_unserved')
        Add-Record $row $lines 'INCUMBENT' @('nominal_seconds','nominal_legacy','nominal_expanded','nominal_bookings',
            'nominal_failed','unassigned_external','nominal_service','nominal_critical_ending','nominal_total_ending')
        $trace=$lines|Where-Object {$_.StartsWith('RESULT,')}|Select-Object -Last 1
        $values=if($trace){$trace.Split(',')}
        $row['distance']=if($values){$values[10]}else{'NA'}
        $row['unsupported_events']=if($values){$values[18]}else{'NA'}
        $row['route_search_nodes_retained_plus_simulation']=if($values){$values[14]}else{'NA'}
        $row['replans']=if($values){$values[19]}else{'NA'}
        $row['average_replan_s']=if($values){$values[27]}else{'NA'}
        [pscustomobject]$row
    }
}
$rows|Export-Csv -NoTypeInformation (Join-Path $root summary.csv)
$rows|Format-Table size,mode,validated,planning_complete,simulation_complete,completed,failed,elapsed_s
