@echo off
rem Players' crash reports, newest first: id, account, game version, what crashed.
rem Optional: crashes-list.bat 50   (how many; default 30)
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set LIMIT=%1
if "%LIMIT%"=="" set LIMIT=30
%AW_SSH% aw-admin crashes list --limit %LIMIT%
endlocal
pause
