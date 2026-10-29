@echo off
rem Ban an account (or an address) from the server. They can't log in until it runs out;
rem someone online right now stays on until kicked (.kick NAME in the game).
rem Durations: 30m, 12h, 7d, 2w, or perm for good.
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set /p NAME=Account name (or type ip to ban an address):
set ADDR=
if /i "%NAME%"=="ip" set /p ADDR=Address:
set /p DURATION=How long (30m, 12h, 7d, 2w or perm):
set /p REASON=Reason (letters, digits, spaces):
%AW_SSH% aw-admin ban %NAME% %ADDR% %DURATION% %REASON%
endlocal
pause
