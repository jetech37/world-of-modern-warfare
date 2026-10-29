@echo off
rem Give an account a new password (the old one cannot be read back, only replaced).
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set NAME=
set PASS=
set /p NAME=Account name:
set /p PASS=New password (6-16 letters, digits or !@#$^*()_+=.,:;?~-):
rem On stdin, never on a command line; cmd /v:on prints it as typed (see create-account.bat).
cmd /v:on /c "echo(!PASS!"| %AW_SSH% aw-admin password %NAME%
set PASS=
endlocal
pause
