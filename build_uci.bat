@echo off
rem Quick build: UCI engine only, x64 Release
setlocal
set SOURCE_DATE_EPOCH=1767225600
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
msbuild ChessEngineUCI.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:m
