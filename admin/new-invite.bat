@echo off
rem Make single-use invite codes. Give one to each new player: they type it into the launcher
rem with the account name and password they pick, and the code is used up.
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set COUNT=1
set NOTE=
set /p COUNT=How many codes (Enter for 1):
set /p NOTE=Note, e.g. who they're for (optional; letters, digits, spaces):
%AW_SSH% aw-admin invite new %COUNT% %NOTE%
endlocal
pause
