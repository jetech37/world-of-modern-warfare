@echo off
rem Azeroth Warfare: start the server (database, login server, world server) in WSL's Docker.
title Azeroth Warfare server

rem Docker Desktop runs the containers; start it if it isn't running.
tasklist /fi "imagename eq Docker Desktop.exe" | find /i "Docker Desktop.exe" >nul
if errorlevel 1 (
    echo Starting Docker Desktop...
    start "" "C:\Program Files\Docker\Docker\Docker Desktop.exe"
)

echo Waiting for Docker...
set /a tries=0
:wait_docker
wsl -d Ubuntu -- docker info >nul 2>&1
if not errorlevel 1 goto docker_ready
set /a tries+=1
if %tries% geq 60 (
    echo Docker did not come up after 3 minutes. Open Docker Desktop and check it, then run this again.
    pause
    exit /b 1
)
timeout /t 3 /nobreak >nul
goto wait_docker
:docker_ready

echo Starting the Azeroth Warfare server...
wsl -d Ubuntu --cd "%~dp0scripts" -- bash ./server.sh start
if errorlevel 1 (
    echo.
    echo The server did not start. Is WSL running? Try: wsl --shutdown, then run this again.
) else (
    echo.
    echo Server is up. Start the game with "Start Client.bat".
)
pause
