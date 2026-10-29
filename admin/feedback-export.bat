@echo off
rem Every open report at once (new, reviewing, accepted), as Markdown or JSON, saved in your user
rem folder and opened in Notepad: ready to read, or to hand to an AI in one go.
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set STATUS=open
set FORMAT=md
set /p STATUS=Which status? (Enter for open; new, reviewing, accepted, declined, done, all): 
set /p FORMAT=Markdown or JSON? (Enter for md, or json): 
set FLAGS=--status %STATUS%
set EXT=md
if /i "%FORMAT%"=="json" set EXT=json
if "%EXT%"=="json" set FLAGS=%FLAGS% --json
set OUT=%USERPROFILE%\womw-feedback.%EXT%
%AW_SSH% aw-admin feedback export %FLAGS% > "%OUT%"
echo Saved to %OUT%
start "" notepad "%OUT%"
endlocal
pause
