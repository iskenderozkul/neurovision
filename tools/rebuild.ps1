# Rebuilds NeuroVision and refreshes dist/NeuroVision-latest.apk.
# Safe to run any time; it never touches source files.
param(
    [switch]$SkipSelfTest,
    [string]$AndroidSdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$JavaHome = "C:\Program Files\Android\Android Studio\jbr"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:JAVA_HOME = $JavaHome
$env:ANDROID_HOME = $AndroidSdk

Write-Output "== build =="
& .\gradlew.bat :app:assembleDebug --console=plain
if ($LASTEXITCODE -ne 0) { throw "assembleDebug failed" }

if (-not $SkipSelfTest) {
    $ndk = Join-Path $AndroidSdk "ndk\27.0.12077973"
    $clang = Join-Path $ndk "toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe"
    $sysroot = Join-Path $ndk "toolchains\llvm\prebuilt\windows-x86_64\sysroot"
    $adb = Join-Path $AndroidSdk "platform-tools\adb.exe"
    if (Test-Path $clang) {
        Write-Output "== native perception self-test (host-built, runs on attached device) =="
        New-Item -ItemType Directory -Force -Path build\host | Out-Null
        & $clang --target=x86_64-linux-android26 --sysroot=$sysroot -std=c++17 -O2 `
            -static-libstdc++ -I app\src\main\cpp `
            -o build\host\selftest_x86_64 `
            tools\selftest_main.cpp app\src\main\cpp\core\perception_selftest.cpp `
            app\src\main\cpp\event\event_engine.cpp app\src\main\cpp\motion\motion_engine.cpp
        if (($adb) -and (Test-Path $adb)) {
            $devices = (& $adb devices) -join "`n"
            if ($devices -match "emulator-|device$") {
                try { & $adb push build\host\selftest_x86_64 /data/local/tmp/selftest_x86_64 2>$null | Out-Null } catch { }
                try { & $adb shell chmod 755 /data/local/tmp/selftest_x86_64 2>$null } catch { }
                & $adb shell /data/local/tmp/selftest_x86_64
            } else {
                Write-Output "(no attached device/emulator; self-test binary built at build\host\selftest_x86_64)"
            }
        }
    }
}

Write-Output "== package =="
New-Item -ItemType Directory -Force -Path dist | Out-Null
Copy-Item app\build\outputs\apk\debug\app-debug.apk dist\NeuroVision-latest.apk -Force
Copy-Item app\build\outputs\apk\debug\app-debug.apk dist\NeuroVision-$((Get-Date).ToString('yyyyMMdd-HHmm'))-debug.apk -Force
Write-Output "OK: dist\NeuroVision-latest.apk"
