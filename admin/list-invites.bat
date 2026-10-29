@echo off
rem Every invite code: unused, used (and by which account) or revoked.
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set ONLY=
set /p ONLY=Only the unused ones? (y/N):
set FLAG=
if /i "%ONLY%"=="y" set FLAG=--unused
%AW_SSH% aw-admin invite list %FLAG%
endlocal
pause
