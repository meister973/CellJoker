@echo off
rem Drag Balatro.exe onto this file. The PKG appears in this folder.
if not exist "%~dp0python\python.exe" (
    echo This file cannot find its other files.
    echo Unzip the whole make_pkg folder first, then drag Balatro.exe onto make_pkg.bat in it.
    pause
    exit /b 1
)
if "%~1"=="" (
    echo Drag Balatro.exe onto this file.
    echo It is in your Steam folder: Steam\steamapps\common\Balatro\Balatro.exe
    pause
    exit /b 2
)
"%~dp0python\python.exe" "%~dp0tool\make_pkg.py" "%~1" "%~dp0."
pause
