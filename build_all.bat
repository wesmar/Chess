@echo off
rem Full release build: eight binaries in bin\, statically linked CRT.
rem GUI and UCI, x64 and x86, Release and Release_MinSize.
rem Toolset and Visual Studio are discovered by find_vs.ps1 - see the comments
rem there for why "latest" alone is not safe on this machine.
setlocal enabledelayedexpansion
cd /d "%~dp0"
set SOURCE_DATE_EPOCH=1767225600

set "CHESS_VS_ROOT="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0find_vs.ps1"`) do set "CHESS_VS_ROOT=%%i"
if not defined CHESS_VS_ROOT (
    echo ERROR: no Visual Studio installation with the C++ toolset was found.
    exit /b 1
)
echo Using: %CHESS_VS_ROOT%
call "%CHESS_VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1

if exist bin del /q bin\*.exe
if not exist bin mkdir bin

call :build Chess.vcxproj          Release          x64    "x64\Release\Chess.exe"                          "bin\Chess_x64.exe"
call :build Chess.vcxproj          Release_MinSize  x64    "x64\Release_MinSize\Chess.exe"                   "bin\Chess_x64_minSize.exe"
call :build Chess.vcxproj          Release          Win32  "Win32\Release\Chess.exe"                         "bin\Chess_x86.exe"
call :build Chess.vcxproj          Release_MinSize  Win32  "Win32\Release_MinSize\Chess.exe"                  "bin\Chess_x86_minSize.exe"
call :build ChessEngineUCI.vcxproj Release          x64    "x64\Release\UCI\ChessEngineUCI.exe"               "bin\ChessEngineUCI_x64.exe"
call :build ChessEngineUCI.vcxproj Release_MinSize  x64    "x64\Release_MinSize\UCI\ChessEngineUCI.exe"        "bin\ChessEngineUCI_x64_minSize.exe"
call :build ChessEngineUCI.vcxproj Release          Win32  "Win32\Release\UCI\ChessEngineUCI.exe"             "bin\ChessEngineUCI_x86.exe"
call :build ChessEngineUCI.vcxproj Release_MinSize  Win32  "Win32\Release_MinSize\UCI\ChessEngineUCI.exe"      "bin\ChessEngineUCI_x86_minSize.exe"

rd /s /q obj 2>nul
rd /s /q x64 2>nul
rd /s /q Win32 2>nul

rem A silent partial build is worse than a loud failure: count what landed.
set /a produced=0
for %%f in (bin\*.exe) do set /a produced+=1
echo.
echo Binaries in bin\: !produced!
if not "!produced!"=="8" (
    echo ERROR: expected 8 binaries.
    exit /b 1
)
dir /b bin\*.exe
echo Build finished.
exit /b 0

:build
rem %1 project  %2 configuration  %3 platform  %4 built path  %5 destination
msbuild %1 /p:Configuration=%2 /p:Platform=%3 /t:Rebuild /m /nologo /v:m
if errorlevel 1 (
    echo ERROR: %1 %2 %3 failed to build.
    exit /b 1
)
if not exist %4 (
    echo ERROR: %1 %2 %3 produced no binary at %4.
    exit /b 1
)
move /y %4 %5 >nul
exit /b 0
