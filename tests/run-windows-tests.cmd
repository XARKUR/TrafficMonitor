@echo off
call "%~1\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++14 /EHsc /W4 /Iinclude tests\gpu_selection_test.cpp /Fe:gpu-selection-test.exe
if errorlevel 1 exit /b %errorlevel%
gpu-selection-test.exe
exit /b %errorlevel%
