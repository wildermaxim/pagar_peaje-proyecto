@echo off
title Servidor Peaje Pamplonita

echo ================================
echo  Iniciando servidor de peaje...
echo ================================

REM Ir a la carpeta donde está el .bat
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

REM Verificar base de datos
if not exist database.db (
    echo Aviso: No existe database.db, se creara automaticamente
)

echo.
echo Servidor corriendo en:
echo http://localhost:8080
echo.
echo Presiona CTRL + C para detener
echo.

lo_que_sea.exe

pause