@echo off
rem Azeroth Warfare: start the server if it isn't running, then the game.
title Azeroth Warfare
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\aw.ps1" play %*
if errorlevel 1 pause
