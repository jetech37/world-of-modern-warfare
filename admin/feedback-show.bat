@echo off
rem One report in full: the player's words, where they stood, their target, their client.
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set ID=
set /p ID=Report number: 
%AW_SSH% aw-admin feedback show %ID%
endlocal
pause
