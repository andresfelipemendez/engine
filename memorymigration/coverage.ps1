param(
    [ValidateSet('All', 'Build', 'Report')]
    [string]$Action = 'All',
    [ValidateSet('all', 'if-only', 'fail')]
    [string]$Scenario = 'all'
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio or Build Tools with the C++ workload and Clang tools.'
}
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath | Select-Object -First 1
if (-not $vs) {
    throw 'In Visual Studio Installer, install the MSVC x64 tools and C++ Clang tools for Windows.'
}
$llvm = Join-Path $vs 'VC\Tools\Llvm\x64\bin'
$clang = Join-Path $llvm 'clang-cl.exe'
$profdata = Join-Path $llvm 'llvm-profdata.exe'
$cov = Join-Path $llvm 'llvm-cov.exe'
foreach ($tool in @($clang, $profdata, $cov)) {
    if (-not (Test-Path $tool)) { throw "LLVM tool not found: $tool" }
}

$out = Join-Path $PSScriptRoot 'build'
$exe = Join-Path $out 'migration_tests.exe'
$raw = Join-Path $out 'migration.profraw'
$profile = Join-Path $out 'migration.profdata'
$html = Join-Path $out 'coverage'
$lcov = Join-Path $out 'lcov.info'
$source = Join-Path $PSScriptRoot 'migration.c'
$tests = Join-Path $PSScriptRoot 'test_migration.c'

if ($Action -ne 'Report') {
    New-Item -ItemType Directory -Force $out | Out-Null
    Remove-Item -Force -ErrorAction SilentlyContinue $raw, $profile

    # VS Code may have inherited a toolset version that was removed by an update.
    Remove-Item Env:VCToolsVersion -ErrorAction SilentlyContinue
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'

    Push-Location $out
    try {
        & $clang /nologo /Od /Zi /clang:-fprofile-instr-generate /clang:-fcoverage-mapping $source $tests /Femigration_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Coverage build failed.' }
    } finally {
        Pop-Location
    }
}

if ($Action -eq 'Build') { return }

$testStatus = 0
if ($Action -eq 'All') {
    $previous = $env:LLVM_PROFILE_FILE
    try {
        $env:LLVM_PROFILE_FILE = $raw
        $testArgs = @(switch ($Scenario) {
            'if-only' { '-t'; 'version_one_targets_two' }
            'fail'    { '--demo-fail' }
        })
        & $exe @testArgs
        $testStatus = $LASTEXITCODE
    } finally {
        $env:LLVM_PROFILE_FILE = $previous
    }
}

if (-not (Test-Path $raw)) {
    if ($Action -eq 'Report') {
        Write-Warning 'No LLVM profile was written. Let the test executable exit normally instead of stopping it.'
        return
    }
    throw 'No LLVM profile was written by the test executable.'
}

& $profdata merge -sparse $raw -o $profile
if ($LASTEXITCODE -ne 0) { throw 'Could not merge the LLVM profile.' }
& $cov show $exe "-instr-profile=$profile" --show-branches=count $source
if ($LASTEXITCODE -ne 0) { throw 'Could not show branch coverage.' }
& $cov report $exe "-instr-profile=$profile" $source
if ($LASTEXITCODE -ne 0) { throw 'Could not summarize coverage.' }
& $cov show $exe "-instr-profile=$profile" --show-branches=count -format=html "-output-dir=$html" $source
if ($LASTEXITCODE -ne 0) { throw 'Could not generate the HTML report.' }
$lcovLines = & $cov export $exe "-instr-profile=$profile" -format=lcov $source
if ($LASTEXITCODE -ne 0) { throw 'Could not export the LCOV report.' }
[IO.File]::WriteAllLines($lcov, [string[]]$lcovLines, [Text.UTF8Encoding]::new($false))
Write-Host "HTML report: $(Join-Path $html 'index.html')"
Write-Host "VS Code coverage: $lcov"
exit $testStatus
