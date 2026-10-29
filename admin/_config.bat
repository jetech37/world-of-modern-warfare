@echo off
rem Reads your server's settings from vps\deploy.env.local (copy vps\deploy.env.example there
rem and fill it in) into AW_VPS, AW_VPS_KEY, AW_GAME_HOST, AW_BACKUP_DEST and the rest, and sets
rem AW_SSH to the ssh command that logs in to the server. A variable already set in the
rem environment wins over the file. The other .bat files here call this first.
set "AW_CONFIG=%~dp0..\vps\deploy.env.local"
if not exist "%AW_CONFIG%" (
    echo No server settings: copy vps\deploy.env.example to vps\deploy.env.local and fill it in.
    exit /b 1
)
for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%AW_CONFIG%") do (
    if not defined %%a set "%%a=%%b"
)
if not defined AW_VPS (
    echo AW_VPS ^(the server's ssh login^) is not set in vps\deploy.env.local.
    exit /b 1
)
rem A leading ~ is your user folder.
if defined AW_VPS_KEY if "%AW_VPS_KEY:~0,1%"=="~" set "AW_VPS_KEY=%USERPROFILE%%AW_VPS_KEY:~1%"
if defined AW_BACKUP_DEST if "%AW_BACKUP_DEST:~0,1%"=="~" set "AW_BACKUP_DEST=%USERPROFILE%%AW_BACKUP_DEST:~1%"
if defined AW_VPS_KEY (
    set "AW_SSH=ssh -i "%AW_VPS_KEY%" %AW_VPS%"
) else (
    set "AW_SSH=ssh %AW_VPS%"
)
exit /b 0
