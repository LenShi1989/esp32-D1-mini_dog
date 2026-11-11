#requires -Version 5.1
<#
.SYNOPSIS
  建立 DogBot 的 SPIFFS 映像檔（spiffs.bin），可直接由網頁 OTA 上傳。

.DESCRIPTION
  使用 ESP32 Arduino core 內附的 mkspiffs 工具，將 esp32_d1_mini_dog/data
  打包成 SPIFFS 映像。預設大小 0x20000（128 KB），對應
  「Minimal SPIFFS (1.9MB APP with OTA)」分割配置。

.PARAMETER DataDir
  前端檔案資料夾，預設 esp32_d1_mini_dog/data。

.PARAMETER Out
  輸出檔路徑，預設 build/spiffs.bin。

.PARAMETER Size
  SPIFFS 分割區大小（十六進位），預設 0x20000。

.PARAMETER Mkspiffs
  mkspiffs.exe 路徑；省略時會自動搜尋 Arduino15 安裝目錄。

.EXAMPLE
  .\tools\build_spiffs.ps1

.EXAMPLE
  .\tools\build_spiffs.ps1 -Size 0x100000 -Out build\big_spiffs.bin
#>
[CmdletBinding()]
param(
  [string]$DataDir = '',
  [string]$Out = '',
  [string]$Size = '0x20000',
  [string]$Mkspiffs = ''
)

$ErrorActionPreference = 'Stop'

$scriptDir = $PSScriptRoot
if (-not $scriptDir) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $DataDir) { $DataDir = Join-Path $scriptDir '..\esp32_d1_mini_dog\data' }
if (-not $Out) { $Out = Join-Path $scriptDir '..\build\spiffs.bin' }

if (-not $Mkspiffs) {
  $roots = @(
    (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\mkspiffs'),
    (Join-Path $env:USERPROFILE 'AppData\Local\Arduino15\packages\esp32\tools\mkspiffs')
  ) | Select-Object -Unique

  foreach ($root in $roots) {
    if (-not (Test-Path $root)) { continue }
    $found = Get-ChildItem -Path $root -Filter 'mkspiffs.exe' -Recurse -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending | Select-Object -First 1
    if ($found) { $Mkspiffs = $found.FullName; break }
  }
}

if (-not $Mkspiffs -or -not (Test-Path $Mkspiffs)) {
  throw '找不到 mkspiffs.exe。請先安裝 ESP32 開發板支援，或以 -Mkspiffs 指定路徑。'
}

$DataDir = [System.IO.Path]::GetFullPath($DataDir)
$Out = [System.IO.Path]::GetFullPath($Out)

if (-not (Test-Path $DataDir)) { throw "找不到 data 資料夾：$DataDir" }

$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path $outDir)) {
  New-Item -ItemType Directory -Path $outDir | Out-Null
}

if (Test-Path $Out) { Remove-Item $Out -Force }

Write-Host "mkspiffs : $Mkspiffs"
Write-Host "data     : $DataDir"
Write-Host "size     : $Size"
Write-Host ''

& $Mkspiffs -c $DataDir -b 4096 -p 256 -s $Size $Out
if ($LASTEXITCODE -ne 0) { throw "mkspiffs 失敗（exit code $LASTEXITCODE）" }

Write-Host ''
& $Mkspiffs -l $Out
if ($LASTEXITCODE -ne 0) { throw "無法讀取映像檔（exit code $LASTEXITCODE）" }

$length = (Get-Item $Out).Length
Write-Host ''
Write-Host ("已建立：{0}（{1:N0} bytes）" -f $Out, $length) -ForegroundColor Green
Write-Host "可直接在網頁「OTA 更新 → SPIFFS .bin」上傳。"
