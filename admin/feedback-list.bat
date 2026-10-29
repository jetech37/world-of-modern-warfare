@echo off
rem Players' bug reports and suggestions, newest first (the Escape menu's Report Bug / Suggest).
rem Statuses: new, reviewing, accepted, declined, done; "open" is the first three, "all" every one.
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set STATUS=all
set TYPE=
set /p STATUS=Which status? (new, reviewing, accepted, declined, done, open; Enter for all): 
set /p TYPE=Which type? (bug, suggestion, other; Enter for all): 
set FLAGS=--status %STATUS%
if not "%TYPE%"=="" set FLAGS=%FLAGS% --type %TYPE%
%AW_SSH% aw-admin feedback list %FLAGS% --limit 100
endlocal
pause
