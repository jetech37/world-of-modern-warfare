@echo off
rem Play on the online server (AW_GAME_HOST in vps\deploy.env.local), from this checkout's
rem dist\AzerothWarfare (package\build.ps1 makes it).
call "%~dp0_config.bat" || (pause & exit /b 1)
title World of Modern Warfare (online)
if not defined AW_GAME_HOST (
    echo AW_GAME_HOST is not set in vps\deploy.env.local.
    pause
    exit /b 1
)
set "AW=%~dp0..\dist\AzerothWarfare\scripts\aw.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%AW%" join %AW_GAME_HOST%
powershell -NoProfile -ExecutionPolicy Bypass -File "%AW%" play
if errorlevel 1 pause
