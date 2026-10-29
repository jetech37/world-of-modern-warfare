@echo off
rem Azeroth Warfare: save every player and stop the server.
title Azeroth Warfare
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\aw.ps1" stop
pause
