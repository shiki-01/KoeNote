# 評価用データの一括評価（要件定義書 11章）。
# tools/eval/data/ にある <名前>.wav と、同じ名前の正解 MIDI（<名前>.mid）の組をすべて評価する。
#
# 使い方（リポジトリのルートで。先に Release ビルドと pip install -r tools/eval/requirements.txt を済ませる）:
#   powershell -ExecutionPolicy Bypass -File tools/eval/run_all.ps1
#   powershell -ExecutionPolicy Bypass -File tools/eval/run_all.ps1 -DataDir D:\vocals -Python py
param(
    [string]$DataDir = "tools/eval/data",
    [string]$OutDir = "tools/eval/out",
    [string]$Analyzer = "build/tools/analyze/koenote_analyze_artefacts/Release/koenote_analyze.exe",
    [string]$Python = "python",
    [string]$EvalArgs = ""   # evaluate.py へ渡す追加の引数（空白区切り。例: -EvalArgs "--merge-ref-notes 0.02"）
)
$extraArgs = @($EvalArgs.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries))

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Analyzer)) { throw "koenote_analyze が見つかりません: $Analyzer（先に Release ビルドを行ってください）" }
if (-not (Test-Path $DataDir)) { throw "評価用データのフォルダがありません: $DataDir" }

New-Item -ItemType Directory -Force $OutDir | Out-Null

$pairs = Get-ChildItem -Path $DataDir -Filter *.wav | Where-Object { Test-Path ([System.IO.Path]::ChangeExtension($_.FullName, ".mid")) }

if ($pairs.Count -eq 0) { throw "$DataDir に、同じ名前の .wav と .mid の組がありません" }

foreach ($wav in $pairs) {
    $name = $wav.BaseName
    $mid = [System.IO.Path]::ChangeExtension($wav.FullName, ".mid")
    Write-Host "=== $name ==="

    # 推定（<OutDir>/<名前>_pitch.csv, <OutDir>/<名前>_notes.csv）と、正解ノートの CSV
    & $Analyzer analyze $wav.FullName $OutDir
    if ($LASTEXITCODE -ne 0) { Write-Warning "解析に失敗しました: $name"; continue }

    $refNotes = Join-Path $OutDir "${name}_ref_notes.csv"
    # <名前>.track があれば、その番号のトラック（MTrk の順番、0 始まり）だけを正解にする（複数パートの MIDI 向け）
    $trackFile = [System.IO.Path]::ChangeExtension($wav.FullName, ".track")
    if (Test-Path $trackFile) {
        & $Analyzer midi $mid $refNotes --track (Get-Content $trackFile -TotalCount 1).Trim()
    } else {
        & $Analyzer midi $mid $refNotes
    }
    if ($LASTEXITCODE -ne 0) { Write-Warning "MIDI の変換に失敗しました: $name"; continue }

    & $Python tools/eval/evaluate.py --ref-notes $refNotes --est-notes (Join-Path $OutDir "${name}_notes.csv") @extraArgs
    if ($LASTEXITCODE -ne 0) { Write-Warning "評価に失敗しました: $name" }
}
