<#
.SYNOPSIS
Exports context-switch CPU accounting after the matched campaign is sealed.
.DESCRIPTION
Uses recorded measurement UTC bounds relative to the ETL start timestamp.
Writes new exports only. Does not rerun games or overwrite sealed captures.
Must run when no xemu process exists, after the measurement queue finishes.
.EXAMPLE
Get-Help .\export-retail-cpu.ps1 -Detailed
#>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$CaptureRoot,
 [Parameter(Mandatory=$true)][string]$OutputRoot,
 [Parameter(Mandatory=$true)][string]$Xperf
)
$ErrorActionPreference='Stop'
if(Get-Process xemu -ErrorAction SilentlyContinue){throw 'Measurement host occupied'}
$root=$OutputRoot
if(Test-Path $root){throw 'Refusing to overwrite CPU exports'}
New-Item -ItemType Directory $root | Out-Null
$captureRoot=$CaptureRoot
$terminal=Get-Content (Join-Path $captureRoot 'receipt.json') -Raw | ConvertFrom-Json
if($terminal.status -ne 'finished-review-evidence'){throw 'Capture worker is not sealed'}
$inputs=@('pgr2-off-vulkan','pgr2-on-vulkan','pgr2-on-opengl','pgr2-off-opengl') | ForEach-Object {Join-Path $captureRoot ($_+'\complete.json')}
$receipt=[ordered]@{status='running';exports=@()}
foreach($inputPath in $inputs){
    $entry=[ordered]@{input=$inputPath;status='running'}
    try {
        $r=Get-Content $inputPath -Raw | ConvertFrom-Json
        $cell=Split-Path (Split-Path $inputPath -Parent) -Leaf
        if($r.measurement_status -ne 'complete' -or $r.etw_lost_events -ne 0 -or $r.etw_lost_buffers -ne 0){throw 'Capture measurement/loss gate failed'}
        $stats=Get-Content $r.trace_stats -Raw
        $m=[regex]::Match($stats,'(?m)^Start time \(UTC\)\s*:\s*([0-9/:.]+)')
        if(-not $m.Success){throw 'ETL start time missing'}
        $culture=[Globalization.CultureInfo]::InvariantCulture
        $styles=[Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal
        $start=[DateTime]::ParseExact($m.Groups[1].Value,'yyyy/MM/dd:HH:mm:ss.fffffff',$culture,$styles)
        $begin=[DateTime]::Parse($r.steady_state_start_utc,$culture,$styles)
        $end=[DateTime]::Parse($r.steady_state_end_utc,$culture,$styles)
        $t1=[long](($begin.Ticks-$start.Ticks)/10)
        $t2=[long](($end.Ticks-$start.Ticks)/10)
        if($t1 -lt 0 -or $t2 -le $t1){throw 'Invalid measurement bounds'}
        $csv=Join-Path $root ($cell+'.cpu.csv')
        $stderr=Join-Path $root ($cell+'.stderr.txt')
        $info=New-Object System.Diagnostics.ProcessStartInfo
        $info.FileName=$Xperf
        $info.Arguments='-i "'+$r.etl+'" -o "'+$csv+'" -a cswitch -process -thread -range '+$t1+' '+$t2
        $info.UseShellExecute=$false
        $info.RedirectStandardError=$true
        $p=New-Object System.Diagnostics.Process
        $p.StartInfo=$info
        if(-not $p.Start()){throw 'CPU exporter did not start'}
        $errorRead=$p.StandardError.ReadToEndAsync()
        if(-not $p.WaitForExit(180000)){$p.Kill();$p.WaitForExit();throw 'CPU exporter timeout'}
        $exitCode=$p.ExitCode
        $errorRead.Result | Set-Content $stderr
        $p.Dispose()
        if($exitCode -ne 0){throw "CPU exporter failed: $exitCode"}
        $entry.status='complete';$entry.pid=$r.xemu_pid;$entry.range_start_us=$t1;$entry.range_end_us=$t2;$entry.output=$csv
    } catch {$entry.status='failed';$entry.error=$_.Exception.Message}
    $receipt.exports+=$entry
    $receipt | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $root 'receipt.json')
}
$receipt.status=if(@($receipt.exports|Where-Object {$_.status -ne 'complete'}).Count){'FAIL'}else{'PASS'}
$receipt | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $root 'receipt.json')
$receipt | ConvertTo-Json -Depth 8
