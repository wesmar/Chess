@echo off
setlocal enabledelayedexpansion
set SOURCE_DATE_EPOCH=1767225600
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -property installationPath`) do set "VS_PATH=%%i"
if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" exit /b 1
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
if exist bin del /q bin\*.exe
if not exist bin mkdir bin
msbuild Chess.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Rebuild /m /nologo
if exist x64\Release\Chess.exe move /y x64\Release\Chess.exe bin\Chess_x64.exe
msbuild Chess.vcxproj /p:Configuration=Release_MinSize /p:Platform=x64 /t:Rebuild /m /nologo
if exist x64\Release_MinSize\Chess.exe move /y x64\Release_MinSize\Chess.exe bin\Chess_x64_minSize.exe
msbuild Chess.vcxproj /p:Configuration=Release /p:Platform=Win32 /t:Rebuild /m /nologo
if exist Win32\Release\Chess.exe move /y Win32\Release\Chess.exe bin\Chess_x86.exe
msbuild Chess.vcxproj /p:Configuration=Release_MinSize /p:Platform=Win32 /t:Rebuild /m /nologo
if exist Win32\Release_MinSize\Chess.exe move /y Win32\Release_MinSize\Chess.exe bin\Chess_x86_minSize.exe
rd /s /q obj
rd /s /q x64
rd /s /q Win32
echo.
echo Build process finished. Binaries are located in 'bin' folder.