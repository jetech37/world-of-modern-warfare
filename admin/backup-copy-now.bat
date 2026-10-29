@echo off
rem Copies the server's nightly database backups to this PC now (to AW_BACKUP_DEST in
rem vps\deploy.env.local; the scheduled task does it daily).
call "%~dp0_config.bat" || (pause & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0pull-backups.ps1"
if defined AW_BACKUP_DEST type "%AW_BACKUP_DEST%\pull.log" 2>nul | more +0
pause
