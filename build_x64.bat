@echo off
rem Development build: one full x64 Release configuration, static CRT, for
rem regression runs and matches. Does NOT touch bin\ - the eight release
rem binaries there come from build_all.bat and nothing else.
rem
rem   build_x64.bat          UCI engine only (default, fastest iteration)
rem   build_x64.bat uci      same
rem   build_x64.bat gui      GUI only
rem   build_x64.bat both     both, for GUI/CLI parity checks before a match
rem
rem Output: x64\Release\UCI\ChessEngineUCI.exe and x64\Release\Chess.exe
setlocal
cd /d "%~dp0"
set SOURCE_DATE_EPOCH=1767225600

set "CHESS_VS_ROOT="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0find_vs.ps1"`) do set "CHESS_VS_ROOT=%%i"
if not defined CHESS_VS_ROOT (
    echo ERROR: no Visual Studio installation with the C++ toolset was found.
    exit /b 1
)
call "%CHESS_VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1

if /i "%~1"=="gui" goto gui

msbuild ChessEngineUCI.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:m
if errorlevel 1 exit /b 1
if /i not "%~1"=="both" exit /b 0

:gui
msbuild Chess.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:m
exit /b %errorlevel%
