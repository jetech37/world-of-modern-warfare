@echo off
rem Azeroth Warfare: start the database, login server and world server.
title Azeroth Warfare
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\aw.ps1" start
pause
