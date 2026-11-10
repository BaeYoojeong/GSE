param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$OutputRoot = (Join-Path ([IO.Path]::GetTempPath()) ('Ashen-Framework-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$gameExecutable = (Resolve-Path -LiteralPath $Executable).Path
if(![IO.Path]::IsPathRooted($OutputRoot) -or (Test-Path -LiteralPath $OutputRoot)) { throw 'OutputRoot must be an absolute path to a new directory.' }
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
foreach($mode in @(@('--window-smoke','window-smoke'),@('--benchmark','frozen'),@('--benchmark-live','moving')))
{
    $destination = Join-Path $OutputRoot $mode[1]
    & $gameExecutable $mode[0] $destination *> (Join-Path $OutputRoot ($mode[1]+'.log'))
    if($LASTEXITCODE -ne 0) { throw ('Check failed: '+$mode[1]+'; see '+$OutputRoot) }
    Write-Output ('Passed: '+$mode[1])
}
Write-Output ('Reports: '+$OutputRoot)
