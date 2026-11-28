@echo off
rem The project's own native build: produces native/build/sample.obj.
rem coorc only compiles the generated bridge and links this object.
if not exist "%~dp0build" mkdir "%~dp0build"
cl /nologo /c /O2 /I"%~dp0include" /Fo"%~dp0build\sample.obj" "%~dp0sample.c"
exit /b %ERRORLEVEL%
