@echo off
rem Builds the web editor and puts it on phoenixanimate.com/webeditor and on
rem Phoenix Cloud, stopping at the first step that fails.
rem
rem   tools\deploy_editor.bat [--skip-tests]
rem
rem --skip-tests skips the cloud's tests (for changes outside cloud\).

setlocal
cd /d "%~dp0.."

python tools\web\build_web.py || goto :failed
python tools\web\deploy_web.py || goto :failed
python tools\deploy_cloud.py deploy --editor %* || goto :failed

echo.
echo The web editor is built and deployed to the site and the cloud.
exit /b 0

:failed
echo.
echo Stopped: that step failed.
exit /b 1
