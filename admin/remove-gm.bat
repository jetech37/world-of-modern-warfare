@echo off
rem Take game master away from an account.
call "%~dp0_config.bat" || (pause & exit /b 1)
set /p NAME=Account name: 
%AW_SSH% aw-admin gm %NAME% 0
pause
