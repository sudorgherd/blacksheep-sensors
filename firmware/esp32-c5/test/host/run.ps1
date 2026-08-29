$ErrorActionPreference = 'Stop'
$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Library = Join-Path $Root 'lib\wifi_frame_parser'
$Output = Join-Path $env:TEMP 'blacksheep-wifi-frame-parser-tests.exe'

$Arguments = @(
  '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1', '-g',
  '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
  "-I$(Join-Path $Library 'include')",
  (Join-Path $Library 'src\wifi_frame_parser.c'),
  (Join-Path $PSScriptRoot 'stage8_oui_feasibility.c'),
  (Join-Path $PSScriptRoot 'test_wifi_frame_parser.c'),
  '-o', $Output
)
& gcc $Arguments
if ($LASTEXITCODE -ne 0) {
  Write-Warning 'GCC sanitizer runtimes are unavailable; running the same suite without sanitizers.'
  $Arguments = @(
    '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2',
    "-I$(Join-Path $Library 'include')",
    (Join-Path $Library 'src\wifi_frame_parser.c'),
    (Join-Path $PSScriptRoot 'stage8_oui_feasibility.c'),
    (Join-Path $PSScriptRoot 'test_wifi_frame_parser.c'),
    '-o', $Output
  )
  & gcc $Arguments
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& $Output
exit $LASTEXITCODE
