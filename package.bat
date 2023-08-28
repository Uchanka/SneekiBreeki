SETLOCAL EnableDelayedExpansion

echo off

set cfg=Production
set src=%~dp0
set dest=_sdk

:loop
IF NOT "%1"=="" (
    IF "%1"=="-debug" (
        SET cfg=Debug
    )
    IF "%1"=="-release" (
        SET cfg=Release
    )
    IF "%1"=="-profiling" (
        SET cfg=Profiling
    )
    IF "%1"=="-relextdev" (
        SET cfg=RelExtDev
    )
    IF "%1"=="-dir" (
        SET dest=%2
        SHIFT
    )
    SHIFT
    GOTO :loop
)

mkdir %src%\%dest%\include
mkdir %src%\%dest%\lib\x64
mkdir %src%\%dest%\bin\x64
mkdir %src%\%dest%\docs
mkdir %src%\%dest%\docs\media
mkdir %src%\%dest%\symbols
mkdir %src%\%dest%\scripts

copy %src%\docs\ProgrammingGuide*.md %src%\%dest%\docs
copy %src%\docs\RTX*.* %src%\%dest%\docs
copy %src%\docs\Streamline*.pdf %src%\%dest%\docs
copy %src%\docs\Debug*.md %src%\%dest%\docs
copy %src%\docs\media\*.* %src%\%dest%\docs\media

copy %src%\include\sl.h %src%\%dest%\include
copy %src%\include\sl_*.h %src%\%dest%\include

copy %src%\_artifacts\sl.interposer\%cfg%_x64\sl.interposer.lib %src%\%dest%\lib\x64\ /Y

del %src%\_sdk\bin\x64\*.* /F /Q

copy %src%\.\README.md %src%\%dest%\bin\x64 /Y

IF "%cfg%"=="Profiling" (
    copy %src%\external\pix\bin\WinPixEventRuntime.dll %src%\%dest%\bin\x64 /Y
)

IF  NOT "%cfg%"=="Production" (
    copy %src%\scripts\sl.*.json %src%\%dest%\bin\x64 /Y
)

copy %src%\_artifacts\sl.common\%cfg%_x64\sl.common.dll %src%\%dest%\bin\x64 /Y
copy %src%\_artifacts\sl.interposer\%cfg%_x64\sl.interposer.dll %src%\%dest%\bin\x64 /Y
copy %src%\_artifacts\sl.mtss_g\%cfg%_x64\sl.mtss_g.dll %src%\%dest%\bin\x64 /Y
IF  NOT "%cfg%"=="Production" (
    copy %src%\_artifacts\sl.imgui\%cfg%_x64\sl.imgui.dll %src%\%dest%\bin\x64 /Y
)

copy %src%\_artifacts\sl.common\%cfg%_x64\sl.common.pdb %src%\%dest%\symbols /Y
copy %src%\_artifacts\sl.interposer\%cfg%_x64\sl.interposer.pdb %src%\%dest%\symbols /Y
copy %src%\_artifacts\sl.mtss_g\%cfg%_x64\sl.mtss_g.pdb %src%\%dest%\symbols /Y
IF  NOT "%cfg%"=="Production" (
    copy %src%\_artifacts\sl.imgui\%cfg%_x64\sl.imgui.pdb %src%\%dest%\symbols /Y
)

echo Configuration:%cfg%
echo Destination:%dest%
