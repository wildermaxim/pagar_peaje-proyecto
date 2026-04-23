@echo off
title Servidor Peaje Pamplonita

echo ================================
echo  Iniciando servidor de peaje...
echo ================================

REM Ir a la carpeta del proyecto (donde está el .bat)
cd /d %~dp0

REM Verificar ejecutable
if not exist lo_que_sea.exe (
    echo ERROR: No se encuentra lo_que_sea.exe
    pause
    exit
)

REM Verificar carpetas necesarias
if not exist html (
    echo ERROR: Falta la carpeta html
    pause
    exit
)

if not exist css (
    echo ERROR: Falta la carpeta css
    pause
    exit
)

echo.
echo Iniciando servidor...
echo.

REM Ejecutar servidor SIN abrir otra consola
start "" /B lo_que_sea.exe

REM Esperar a que arranque
timeout /t 2 >nul

REM Abrir navegador
start http://localhost:8080

echo.
echo Servidor corriendo en:
echo http://localhost:8080
echo.
echo Presiona CTRL + C para detener
echo.

pause