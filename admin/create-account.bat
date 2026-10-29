@echo off
rem Make a game account for a friend. Players can't sign themselves up: this account is what
rem they log in to the game with, and what the launcher and the site's download ask for.
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set NAME=
set PASS=
set LEVEL=0
set /p NAME=Account name (3-16 letters or digits):
set /p PASS=Password (6-16 letters, digits or !@#$^*()_+=.,:;?~-):
set /p LEVEL=GM level 0-6 (Enter for 0, a player; 3 game master, 6 full admin):
rem The password goes over ssh on stdin, never on a command line. cmd /v:on prints it by delayed
rem expansion, so characters like ^ ( ) reach the server as typed.
cmd /v:on /c "echo(!PASS!"| %AW_SSH% aw-admin create %NAME% %LEVEL%
set PASS=
endlocal
pause
