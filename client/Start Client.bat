@echo off
rem Azeroth Warfare: start the game client against the local server.
rem Uses the WoW install linked at .\WoW and the Steam copy of Modern Warfare 2.
title Azeroth Warfare client
cd /d "%~dp0"

set "WOW_DATA=%~dp0WoW\Data"
set "MW2_PATH=C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare 2"
set "WOW_HOST=localhost"

if not exist "target\release\benilla-mw2.exe" (
    echo The client is not built yet; building it now. This takes a few minutes the first time.
    cargo build --release
    if errorlevel 1 (
        echo Build failed.
        pause
        exit /b 1
    )
)

start "" "target\release\benilla-mw2.exe"
