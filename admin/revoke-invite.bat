@echo off
rem Cancel an invite code nobody has used yet (an account made with a used code stays).
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set CODE=
set /p CODE=Invite code to cancel:
%AW_SSH% aw-admin invite revoke %CODE%
endlocal
pause
