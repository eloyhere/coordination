@echo off
rem The project's own native build: produces native/build/counter.obj.
if not exist "%~dp0build" mkdir "%~dp0build"
cl /nologo /std:c++20 /EHsc /c /O2 /I"%~dp0include" /Fo"%~dp0build\counter.obj" "%~dp0counter.cpp"
exit /b %ERRORLEVEL%
