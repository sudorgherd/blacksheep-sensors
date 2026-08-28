param(
  [Parameter(Mandatory = $true)]
  [string]$Transmitter,
  [Parameter(Mandatory = $true)]
  [string]$Bssid
)

$ErrorActionPreference = 'Stop'

function Convert-Address([string]$Value) {
  if ($Value -notmatch '^(?i:[0-9a-f]{2})(:(?i:[0-9a-f]{2})){5}$') {
    throw 'Address must contain exactly six colon-separated hexadecimal octets.'
  }
  return ($Value.Split(':') | ForEach-Object {
    '0x{0:X2}' -f [Convert]::ToByte($_, 16)
  }) -join ', '
}

$TransmitterBytes = Convert-Address $Transmitter
$BssidBytes = Convert-Address $Bssid
$Output = Join-Path $PSScriptRoot '..\src\stage5_local_config.h'
$Contents = @"
#ifndef STAGE5_LOCAL_CONFIG_H
#define STAGE5_LOCAL_CONFIG_H
#define WIFI_STAGE5_CONTROLLED_SOURCE_ENABLED 1
#define WIFI_STAGE5_CONTROLLED_TRANSMITTER {$TransmitterBytes}
#define WIFI_STAGE5_CONTROLLED_BSSID {$BssidBytes}
#endif
"@
Set-Content -LiteralPath $Output -Value $Contents -Encoding ascii
Write-Output 'Stage 5 local controlled-source configuration written (address values suppressed).'
