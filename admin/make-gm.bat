@echo off
rem Make an account a game master (they relog to get it).
rem Levels: 3 game master (.go, companions, plots), 4 and up add .modify money and more, 6 full admin.
call "%~dp0_config.bat" || (pause & exit /b 1)
set /p NAME=Account name: 
set LEVEL=6
set /p LEVEL=GM level 1-6 (Enter for 6, full admin): 
%AW_SSH% aw-admin gm %NAME% %LEVEL%
pause
