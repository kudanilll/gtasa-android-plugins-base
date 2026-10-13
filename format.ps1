$PSScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

# 1. Resolve clang-format executable
$clangFormat = $null

if (Get-Command clang-format -ErrorAction SilentlyContinue) {
    $clangFormat = (Get-Command clang-format).Source
}

if (-not $clangFormat -and (Test-Path "$PSScriptRoot/NDKPath.txt")) {
    $ndkPath = (Get-Content "$PSScriptRoot/NDKPath.txt").Trim()
    $candidate = Join-Path $ndkPath "toolchains\llvm\prebuilt\windows-x86_64\bin\clang-format.exe"
    if (Test-Path $candidate) {
        $clangFormat = $candidate
    }
}

if (-not $clangFormat) {
    Write-Error "[FORMAT] clang-format not found in PATH or NDK toolchain."
    Exit 1
}

Write-Output "[FORMAT] Using clang-format: $clangFormat"

# 2. Collect files to format (excluding build artifacts, git, thirdparty, etc.)
$extensions = @("*.cpp", "*.h", "*.cc", "*.hpp")
$excludeDirs = @(".git", "libs", "obj", ".vscode", "thirdparty")

$files = Get-ChildItem -Path $PSScriptRoot -Recurse -File -Include $extensions | Where-Object {
    $filePath = $_.FullName
    $skip = $false
    foreach ($dir in $excludeDirs) {
        if ($filePath -like "*\$dir\*") {
            $skip = $true
            break
        }
    }
    -not $skip
}

Write-Output "[FORMAT] Formatting $($files.Count) files..."

foreach ($file in $files) {
    & $clangFormat -i -style=file "$($file.FullName)"
}

Write-Output "[FORMAT] All files formatted successfully!"
Exit 0
