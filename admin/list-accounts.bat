@echo off
rem Every account: GM level, how many characters, online, joined, last login.
call "%~dp0_config.bat" || (pause & exit /b 1)
%AW_SSH% aw-admin accounts
pause
