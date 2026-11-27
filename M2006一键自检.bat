@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0m2006-self-test.ps1"
set "testExit=%errorlevel%"
pause
exit /b %testExit%
