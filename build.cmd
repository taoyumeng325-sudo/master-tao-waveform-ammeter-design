@echo off
setlocal
pushd "%~dp0" || exit /b 1
where idf.py >nul 2>nul
if errorlevel 1 (
    echo Open an ESP-IDF terminal, then run build.cmd again.
    popd
    exit /b 1
)
if not exist sdkconfig (
    call idf.py set-target esp32p4
    if errorlevel 1 goto fail
)
findstr /b /c:"CONFIG_IDF_TARGET_ESP32P4=y" sdkconfig >nul
if errorlevel 1 (
    echo sdkconfig does not target ESP32-P4. Review it before rebuilding.
    goto fail
)
call idf.py build
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
