@echo off
REM Compatibility entry: all paths are now explicit; signing is opt-in.
python "%~dp0..\..\tools\release\package_windows.py" %*
exit /b %errorlevel%
