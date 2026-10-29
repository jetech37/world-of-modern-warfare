@echo off
rem Azeroth Warfare: stop the login and world servers (the database keeps running; its data is untouched).
title Azeroth Warfare server
echo Stopping the Azeroth Warfare server...
wsl -d Ubuntu --cd "%~dp0scripts" -- bash ./server.sh stop
pause
