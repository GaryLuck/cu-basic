@echo off
REM Build Tiny BASIC interpreter with Microsoft MSVC
REM Run from Developer Command Prompt for VS, or ensure cl.exe is in PATH

cl /nologo /W3 /Fe:basic.exe basic.c
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo Build succeeded: basic.exe
