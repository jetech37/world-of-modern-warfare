@echo off
rem Play on your own local server (starts it if needed), from this checkout's dist\AzerothWarfare
rem (package\build.ps1 makes it).
title World of Modern Warfare (local)
set "AW=%~dp0..\dist\AzerothWarfare\scripts\aw.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%AW%" join localhost
powershell -NoProfile -ExecutionPolicy Bypass -File "%AW%" play
if errorlevel 1 pause
