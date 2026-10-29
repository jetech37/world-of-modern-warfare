@echo off
rem Azeroth Warfare: make or reset a login account: Create Account NAME PASSWORD [GM level 0-6].
title Azeroth Warfare
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\aw.ps1" account %*
pause
