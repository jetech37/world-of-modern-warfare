@echo off
rem Recent crashes grouped by what crashed, as Markdown, saved to crashes.md here (for triage).
call "%~dp0_config.bat" || (pause & exit /b 1)
%AW_SSH% aw-admin crashes export > "%~dp0crashes.md"
echo Saved to %~dp0crashes.md
pause
