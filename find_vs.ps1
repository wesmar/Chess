# Locate a Visual Studio installation that really carries the C++ toolset.
#
# Why not just "vswhere -latest": on this class of machine vswhere -products *
# also lists SQL Server Management Studio, whose installationVersion (22.x)
# sorts ABOVE Visual Studio (18.x). Anything that trusts version order lands on
# an install with no compiler. Requiring the component id
# Microsoft.VisualStudio.Component.VC.Tools.x86.x64 is no better - newer VS
# releases do not report it, and the query comes back empty.
#
# So the selection is by evidence: an installation counts only if
# VC\Auxiliary\Build\vcvarsall.bat exists AND some VC\Tools\MSVC\<ver> under it
# actually contains cl.exe. Among those, the newest toolset wins, compared as
# [version] rather than as text, so 14.50 beats 14.9.
#
# Prints the chosen installation root on stdout, exits 1 when nothing qualifies.
$ErrorActionPreference = 'Stop'

$roots = New-Object System.Collections.Generic.List[string]

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    foreach ($line in (& $vswhere -products * -all -format value -property installationPath)) {
        if ($line) { $roots.Add($line.Trim()) }
    }
}

# Fallback for machines where the installer was removed or VS was relocated:
# walk the usual layout <ProgramFiles>\Microsoft Visual Studio\<year|major>\<edition>.
foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
    if (-not $base) { continue }
    $dir = Join-Path $base 'Microsoft Visual Studio'
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    foreach ($major in Get-ChildItem -LiteralPath $dir -Directory -ErrorAction SilentlyContinue) {
        foreach ($edition in Get-ChildItem -LiteralPath $major.FullName -Directory -ErrorAction SilentlyContinue) {
            $roots.Add($edition.FullName)
        }
    }
}

$bestRoot = $null
$bestVersion = $null

foreach ($root in ($roots | Sort-Object -Unique)) {
    if (-not (Test-Path -LiteralPath (Join-Path $root 'VC\Auxiliary\Build\vcvarsall.bat'))) { continue }
    $msvc = Join-Path $root 'VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $msvc)) { continue }

    foreach ($toolset in Get-ChildItem -LiteralPath $msvc -Directory -ErrorAction SilentlyContinue) {
        # The compiler binary is the evidence. A folder name proves nothing.
        if (-not (Test-Path -LiteralPath (Join-Path $toolset.FullName 'bin\Hostx64\x64\cl.exe'))) { continue }
        $parsed = $null
        if (-not [version]::TryParse($toolset.Name, [ref]$parsed)) { continue }
        if ($null -eq $bestVersion -or $parsed -gt $bestVersion) {
            $bestVersion = $parsed
            $bestRoot = $root
        }
    }
}

if (-not $bestRoot) {
    Write-Error 'No Visual Studio installation with the C++ toolset was found.'
    exit 1
}

Write-Output $bestRoot
