@echo off
rem One crash report: who, which version, and the game's crash text (an id from crashes-list.bat).
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set ID=%1
if "%ID%"=="" set /p ID=Crash report id (from crashes-list.bat):
%AW_SSH% aw-admin crashes show %ID%
endlocal
pause
