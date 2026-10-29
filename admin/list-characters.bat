@echo off
rem Characters with race, class, level, gold, where they are and time played.
call "%~dp0_config.bat" || (pause & exit /b 1)
set NAME=
set /p NAME=Account name (leave empty for every account): 
%AW_SSH% aw-admin chars %NAME%
pause
