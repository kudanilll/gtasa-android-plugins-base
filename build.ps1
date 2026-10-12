$NDKPath = (Get-Content "$PSScriptRoot/NDKPath.txt").Trim()
Write-Output "NDK located at: $NDKPath"

$projectDir = $PSScriptRoot
if ($projectDir -match '\s') {
    try {
        $fso = New-Object -ComObject Scripting.FileSystemObject
        $projectDir = $fso.GetFolder($projectDir).ShortPath
    } catch {
        Write-Warning "Failed to convert project path with spaces to 8.3 short path: $_"
    }
}

$buildScript = Join-Path $NDKPath "build\ndk-build.cmd"
if (-not (Test-Path $buildScript)) {
    $buildScript = Join-Path $NDKPath "ndk-build.cmd"
}

Write-Output "[BUILD] Starting NDK from $projectDir..."
& $buildScript "NDK_PROJECT_PATH=$projectDir" "APP_BUILD_SCRIPT=$projectDir/Android.mk" "NDK_APPLICATION_MK=$projectDir/Application.mk" "NDK_DEBUG=0"
Write-Output "[BUILD] Done!"

Exit $LASTEXITCODE
