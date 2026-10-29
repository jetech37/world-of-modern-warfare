@echo off
rem Lift a ban on an account (or an address).
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set /p NAME=Account name (or type ip to unban an address):
set ADDR=
if /i "%NAME%"=="ip" set /p ADDR=Address:
%AW_SSH% aw-admin unban %NAME% %ADDR%
endlocal
pause
