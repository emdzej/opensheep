@echo off
rem OpenSheep @VERSION@ on gasm-run @GASM_VERSION@. Double-click to play; OpenSheep.cmd --help for the options.
rem The work is done by opensheep.ps1 (CD picker, saved CD location, gasm-run command line).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0opensheep.ps1" %*
set rc=%errorlevel%
rem Keep the window open on errors when started by double-click (OPENSHEEP_NO_PAUSE=1 skips this).
if not "%rc%"=="0" if not "%OPENSHEEP_NO_PAUSE%"=="1" pause
exit /b %rc%
