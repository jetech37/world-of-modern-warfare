@echo off
rem Players' legal-access attestations (the box they tick or the Y they type at sign-up, or once
rem in the launcher): account, when, from which address, which version of the statement.
rem Optional: attestations.bat NAME   (one account's)
call "%~dp0_config.bat" || (pause & exit /b 1)
setlocal
set NAME=%1
if "%NAME%"=="" set /p NAME=Account name (Enter for every account): 
set FLAGS=
if not "%NAME%"=="" set FLAGS=--account %NAME%
%AW_SSH% aw-admin attestations %FLAGS%
endlocal
pause
