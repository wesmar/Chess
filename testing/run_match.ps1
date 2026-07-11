# run_match.ps1 - SPRT match between two engine builds using cutechess-cli
#
# Usage:
#   .\run_match.ps1 -New path\to\new.exe -Base path\to\base.exe
#   .\run_match.ps1 -New new.exe -Base base.exe -Rounds 500 -Tc "8+0.08" -Concurrency 5
#
# Each engine directory gets a settings.ini (Level 10, 1 thread) so results
# measure search strength, not thread count. The match stops early once SPRT
# accepts H1 (new is at least +5 Elo) or H0 (no gain).

param(
    [Parameter(Mandatory)] [string]$New,
    [Parameter(Mandatory)] [string]$Base,
    [int]$Rounds = 500,
    [string]$Tc = "8+0.08",
    [int]$Concurrency = 5,
    [int]$Threads = 1,
    [string]$Out = "match"
)

$root = $PSScriptRoot
$cutechess = Join-Path $root "..\tools\cutechess\cutechess-1.4.0-win64\cutechess-cli.exe"
$openings = Join-Path $root "openings.pgn"

# Stage each engine in its own directory with tournament settings
foreach ($pair in @(@("new", $New), @("base", $Base))) {
    $dir = Join-Path $root $pair[0]
    New-Item -ItemType Directory -Force $dir | Out-Null
    Copy-Item $pair[1] (Join-Path $dir "engine.exe") -Force
    Set-Content (Join-Path $dir "settings.ini") "[Game]`r`nAIDifficulty=10`r`nThreads=$Threads`r`nUseNeuralEval=0"
}

& $cutechess `
    -engine name=NEW cmd="$root\new\engine.exe" dir="$root\new" `
    -engine name=BASE cmd="$root\base\engine.exe" dir="$root\base" `
    -each proto=uci tc=$Tc option.Threads=$Threads timemargin=200 `
    -games 2 -rounds $Rounds -repeat 2 `
    -sprt elo0=0 elo1=5 alpha=0.05 beta=0.05 `
    -openings file="$openings" format=pgn order=random `
    -concurrency $Concurrency `
    -draw movenumber=60 movecount=8 score=15 `
    -resign movecount=5 score=600 `
    -ratinginterval 25 `
    -pgnout "$root\$Out.pgn" 2>&1 | Tee-Object "$root\$Out.log"
