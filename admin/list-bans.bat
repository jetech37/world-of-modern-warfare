@echo off
rem Every ban in force: accounts and addresses.
call "%~dp0_config.bat" || (pause & exit /b 1)
%AW_SSH% aw-admin bans
pause
