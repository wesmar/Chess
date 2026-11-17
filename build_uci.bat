@echo off
rem Quick build: UCI engine only, x64 Release
setlocal
call "%~dp0build_x64.bat" uci
exit /b %errorlevel%
