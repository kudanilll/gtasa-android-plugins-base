# Creates a new mod from this template:
#   powershell -c "irm https://raw.githubusercontent.com/kudanilll/gtasa-android-plugins-base/master/create.ps1 | iex"
# Runs on Windows PowerShell 5.1 and PowerShell 7. Keep the steps in sync with create.sh.
# Keep this file ASCII: Windows PowerShell 5.1 misreads UTF-8 files that have no BOM.
# Everything runs inside one script block, so `irm | iex` leaves no variables behind and never closes the shell.
& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue' # the 5.1 progress bar slows downloads to a crawl

    $repo = 'kudanilll/gtasa-android-plugins-base'
    $branch = 'master'
    $commitMessage = 'Initialise project from the AML mod template by 0xavion'

    $check = [char]0x2714
    $cross = [char]0x2717
    $dot = [char]0x25CF
    $arrow = [char]0x00BB
    # "AML for GTASA ANDROID": A light green, M blue, L light red, "for" gray, the rest white.
    # Segments are split by '|'; '#', '^' and '_' stand for the full, upper and lower half blocks.
    $glyphs = @{ '#' = [char]0x2588; '^' = [char]0x2580; '_' = [char]0x2584 }
    $bannerColors = 'Green', 'Blue', 'Red', 'DarkGray', 'White'
    $banner = @(
        '_^#|#^_^#|#  |   |#^^ ^#^ _^# #^ _^#   _^# #_ # #^_ #^# #^# # #^_'
        '#^#|# ^ #|#__|for|#_#  #  #^# _# #^#   #^# # ^# #_^ #^_ #_# # #_^'
    )

    function Write-Step($text) {
        Write-Host "$dot " -ForegroundColor Cyan -NoNewline
        Write-Host "$text... " -NoNewline
    }

    function Write-Warn($text) { Write-Host "  ! $text" -ForegroundColor Yellow }

    # Asks until $validate returns nothing (valid), then returns the answer.
    function Read-Answer($label, $default, [scriptblock]$validate, [switch]$optional) {
        while ($true) {
            Write-Host '? ' -ForegroundColor Magenta -NoNewline
            Write-Host "$label " -NoNewline
            if ($default) { Write-Host "($default) " -ForegroundColor DarkGray -NoNewline }
            Write-Host "$arrow " -NoNewline
            $reply = Read-Host
            if ($null -eq $reply) { throw 'Input ended' }
            $reply = $reply.Trim()
            if (-not $reply) { $reply = $default }
            if (-not $reply -and -not $optional) { $problem = 'This field is required' }
            else { $problem = & $validate $reply }
            if (-not $problem) { return $reply }
            Write-Host "  $cross $problem" -ForegroundColor Red
        }
    }

    function Get-Slug($text) { ($text.ToLowerInvariant() -creplace '[^a-z0-9_]', '') }

    function Test-Ndk($dir) {
        [bool]$dir -and ((Test-Path -LiteralPath (Join-Path $dir 'ndk-build.cmd')) -or
            (Test-Path -LiteralPath (Join-Path $dir 'build\ndk-build.cmd')))
    }

    function Find-Ndk {
        foreach ($dir in $env:ANDROID_NDK_HOME, $env:ANDROID_NDK_ROOT, $env:ANDROID_NDK) {
            if (Test-Ndk $dir) { return $dir }
        }
        $sdks = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)
        if ($env:LOCALAPPDATA) { $sdks += Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
        foreach ($sdk in $sdks) {
            if (-not $sdk) { continue }
            $ndkRoot = Join-Path $sdk 'ndk'
            if (Test-Path -LiteralPath $ndkRoot) {
                $newestFirst = Get-ChildItem -LiteralPath $ndkRoot -Directory | Sort-Object -Descending {
                    $version = $null
                    if ([version]::TryParse($_.Name, [ref]$version)) { $version } else { [version]'0.0' }
                }
                foreach ($dir in $newestFirst) { if (Test-Ndk $dir.FullName) { return $dir.FullName } }
            }
            $bundle = Join-Path $sdk 'ndk-bundle'
            if (Test-Ndk $bundle) { return $bundle }
        }
    }

    # Case-sensitive regex replace that warns when the template no longer has the pattern.
    $utf8 = New-Object Text.UTF8Encoding $false
    function Set-Text($dest, $file, $pattern, $replacement) {
        $path = Join-Path $dest $file
        $text = [IO.File]::ReadAllText($path)
        if ($text -cnotmatch $pattern) { Write-Warn "'$pattern' not found in $file"; return }
        [IO.File]::WriteAllText($path, ($text -creplace $pattern, $replacement), $utf8)
    }

    function Invoke-Git {
        $ErrorActionPreference = 'Continue' # 5.1 turns redirected stderr into errors
        & git @args 2>$null | Out-Null
        $LASTEXITCODE -eq 0
    }

    $textRule = { param($v) if ($v -cnotmatch '^[A-Za-z0-9][A-Za-z0-9 ._-]*$') { 'Use letters, digits, spaces, dots, dashes or underscores' } }
    $versionRule = { param($v) if ($v -cnotmatch '^[A-Za-z0-9._-]+$') { 'Use letters, digits, dots, dashes or underscores (no spaces)' } }
    $idRule = { param($v) if ($v -cnotmatch '^[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)*$') { 'Use letters, digits and underscores, separated by dots' } }
    $libRule = { param($v) if ($v -cnotmatch '^[A-Za-z0-9_]+$') { 'Use letters, digits or underscores' } }
    $folderRule = {
        param($v)
        if ($v -cnotmatch '^[A-Za-z0-9_][A-Za-z0-9._-]*$') { 'Use letters, digits, dots, dashes or underscores' }
        elseif ((Test-Path -LiteralPath $v) -and (Get-ChildItem -LiteralPath $v -Force | Select-Object -First 1)) {
            "$v already exists and is not empty"
        }
    }
    $ndkRule = { param($v) if ($v -and -not (Test-Ndk $v)) { 'No ndk-build.cmd in that folder' } }

    $oldEncoding = [Console]::OutputEncoding
    try { [Console]::OutputEncoding = New-Object Text.UTF8Encoding $false } catch {}
    try {
        foreach ($line in $banner) {
            $segments = $line.Split('|')
            for ($i = 0; $i -lt $segments.Count; $i++) {
                $text = -join ($segments[$i].ToCharArray() | ForEach-Object { if ($glyphs.ContainsKey([string]$_)) { $glyphs[[string]$_] } else { $_ } })
                if ($i -lt $segments.Count - 1) { $text += ' ' }
                Write-Host $text -ForegroundColor $bannerColors[$i] -NoNewline
            }
            Write-Host ''
        }
        Write-Host "By 0xavion`n" -ForegroundColor DarkGray

        $name = Read-Answer 'Mod name' '' $textRule
        $author = Read-Answer 'Author' '' $textRule
        $version = Read-Answer 'Version' '1.0' $versionRule
        $lib = Get-Slug $name
        if (-not $lib) { $lib = 'mymod' }
        $authorSlug = Get-Slug $author
        $id = Read-Answer 'Mod ID' $(if ($authorSlug) { "$authorSlug.$lib" } else { $lib }) $idRule
        $lib = Read-Answer 'Library name' $lib $libRule
        $folder = Read-Answer 'Folder' $lib $folderRule
        $pascal = $name -replace ' ', ''
        Write-Host ''

        # .NET resolves relative paths against the process directory, not PowerShell's location.
        $dest = Join-Path (Get-Location).ProviderPath $folder
        $existed = Test-Path -LiteralPath $dest
        # Unpack next to the destination: 5.1 cannot move folders across drives.
        $temp = "$dest." + [IO.Path]::GetRandomFileName()
        $zip = "$temp.zip"

        Write-Step 'Downloading template'
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest "https://github.com/$repo/archive/refs/heads/$branch.zip" -OutFile $zip -UseBasicParsing
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [IO.Compression.ZipFile]::ExtractToDirectory($zip, $temp)
            $root = Get-ChildItem -LiteralPath $temp -Directory | Select-Object -First 1
            New-Item -ItemType Directory -Force -Path $dest | Out-Null
            Get-ChildItem -LiteralPath $root.FullName -Force | Move-Item -Destination $dest
            Remove-Item -LiteralPath (Join-Path $dest 'create.sh'), (Join-Path $dest 'create.ps1') -ErrorAction SilentlyContinue
        } catch {
            Write-Host 'failed' -ForegroundColor Red
            if (-not $existed) { Remove-Item -LiteralPath $dest -Recurse -Force -ErrorAction SilentlyContinue } # only what this run created
            throw
        } finally {
            Remove-Item -LiteralPath $zip, $temp -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Host 'done' -ForegroundColor Green

        Write-Step 'Applying names'
        Set-Text $dest 'Android.mk' '(?m)^LOCAL_MODULE\s*:=.*$' "LOCAL_MODULE    := $lib"
        Set-Text $dest 'main.cpp' '(?m)^MYMOD\(.*\)' "MYMOD($id, $pascal, $version, $author)"
        Set-Text $dest 'main.cpp' 'Mod Template' $name
        Set-Text $dest '.github\workflows\release.yml' 'libmodtemplate' "lib$lib"
        Set-Text $dest '.github\workflows\release.yml' 'BasePlugin-libs' "$pascal-libs"
        Set-Text $dest 'README.md' 'libmodtemplate' "lib$lib"
        Set-Text $dest 'README.md' '\A# [^\n]*' "# $name"
        # Drop the template-only "Create a new mod" section.
        $readme = Join-Path $dest 'README.md'
        [IO.File]::WriteAllText($readme, ([IO.File]::ReadAllText($readme) -creplace '(?ms)^## Create a new mod\n.*?(?=^## )', ''), $utf8)
        Write-Host 'done' -ForegroundColor Green

        $ndk = Find-Ndk
        if ($ndk) {
            Write-Step 'NDK found'
            Write-Host $ndk -ForegroundColor Green
        } else {
            Write-Warn 'Android NDK not found'
            $ndk = Read-Answer 'Android NDK path (empty to skip)' '' $ndkRule -optional
        }
        if ($ndk) { [IO.File]::WriteAllText((Join-Path $dest 'NDKPath.txt'), $ndk, $utf8) }
        else { Write-Warn "Skipped. Put your NDK path in $folder\NDKPath.txt before building" }

        if (Get-Command git -ErrorAction SilentlyContinue) {
            Write-Step 'git init + first commit'
            if (-not (Invoke-Git -C $dest init -q)) { Write-Host 'skipped (git init failed)' -ForegroundColor Yellow }
            elseif ((Invoke-Git -C $dest add -A) -and (Invoke-Git -C $dest commit -q -m $commitMessage)) { Write-Host 'done' -ForegroundColor Green }
            else { Write-Host 'initialised, commit skipped (set git user.name and user.email)' -ForegroundColor Yellow }
        }

        Write-Host "`n$check Created $folder`n" -ForegroundColor Green
        Write-Host "  Next steps:`n    cd $folder`n    .\build.ps1`n"
    } catch {
        Write-Host "`n$cross $($_.Exception.Message)" -ForegroundColor Red
    } finally {
        try { [Console]::OutputEncoding = $oldEncoding } catch {}
    }
}
