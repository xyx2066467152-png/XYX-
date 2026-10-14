param([Parameter(ValueFromRemainingArguments=$true)][string[]]$MonitorArguments)
$ErrorActionPreference='Stop'
$pythonPath=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
if(-not (Test-Path -LiteralPath $pythonPath)){$pythonPath=(Get-Command python -ErrorAction Stop).Source}
$previousPath=$env:PYTHONPATH
try{
 $env:PYTHONPATH=Join-Path $PSScriptRoot 'pc_dependencies'
 & $pythonPath (Join-Path $PSScriptRoot 'pc_uart_monitor.py') @MonitorArguments
 $monitorExit=$LASTEXITCODE
}finally{$env:PYTHONPATH=$previousPath}
exit $monitorExit
