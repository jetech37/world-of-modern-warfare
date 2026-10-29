@echo off
rem Mark a report: new, reviewing, accepted, declined or done, with an optional note.
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set ID=
set STATUS=
set NOTE=
set /p ID=Report number: 
set /p STATUS=New status (new, reviewing, accepted, declined, done): 
set /p NOTE=Note (Enter for none): 
rem The note goes over ssh on stdin (the - below), never on a command line, so quotes and other
rem characters reach the server as typed (see create-account.bat).
cmd /v:on /c "echo(!NOTE!"| %AW_SSH% aw-admin feedback set %ID% %STATUS% -
endlocal
pause
