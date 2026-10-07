@echo off
setlocal
cd /d "%~dp0.."
if not exist logs mkdir logs
set "TASK_REPLAY_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "TASK_REPLAY_VSPATH="
for /f "usebackq tokens=*" %%i in (`"%TASK_REPLAY_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_REPLAY_VSPATH=%%i"
if not defined TASK_REPLAY_VSPATH exit /b 1
set VSCMD_SKIP_SENDTELEMETRY=1
call "%TASK_REPLAY_VSPATH%\VC\Auxiliary\Build\vcvars64.bat" > logs\vcvars_replay_gs.log 2>&1
if errorlevel 1 exit /b 1
if defined GOW_WORK (set "TASK_REPLAY_WORK=%GOW_WORK%") else (set "TASK_REPLAY_WORK=%~d0\gowport")
set "TASK_REPLAY_BUILD=%TASK_REPLAY_WORK%\PS2Recomp\out\build"
if not exist "%TASK_REPLAY_BUILD%\ps2xRuntime\ps2_runtime.lib" exit /b 1
call :compile tools\render\repetir_gs.cpp repetir_gs
if errorlevel 1 exit /b 1
call :compile tests\gs_replay_test.cpp gs_replay_test
if errorlevel 1 exit /b 1
call :compile tools\render\comparar_feedback_gs.cpp comparar_feedback_gs
if errorlevel 1 exit /b 1
call :compile tests\gs_frame_pixels_test.cpp gs_frame_pixels_test
if errorlevel 1 exit /b 1
logs\gs_frame_pixels_test.exe > logs\gs_frame_pixels_test.log 2>&1
if errorlevel 1 exit /b 1
logs\gs_replay_test.exe logs\gs_replay_synthetic.bin > logs\gs_replay_test.log 2>&1
if errorlevel 1 exit /b 1
logs\repetir_gs.exe logs\gs_replay_synthetic.bin cpu --lockstep logs >> logs\gs_replay_test.log 2>&1
exit /b %errorlevel%

:compile
cl /nologo /std:c++20 /EHsc /O2 /MD /utf-8 /I "%TASK_REPLAY_WORK%\PS2Recomp\ps2xRuntime\include" /I src /c "%~1" /Fo"logs\%~2.obj" > "logs\%~2_build.log" 2>&1
if errorlevel 1 exit /b 1
link /nologo /out:"logs\%~2.exe" "logs\%~2.obj" "%TASK_REPLAY_BUILD%\ps2xRuntime\ps2_runtime.lib" "%TASK_REPLAY_BUILD%\_deps\fmt-build\fmt.lib" "%TASK_REPLAY_BUILD%\_deps\raylib-build\raylib\raylib.lib" "%TASK_REPLAY_BUILD%\ps2xIOP\ps2_iop.lib" glu32.lib winmm.lib opengl32.lib gdi32.lib user32.lib shell32.lib ole32.lib advapi32.lib bcrypt.lib secur32.lib ws2_32.lib >> "logs\%~2_build.log" 2>&1
exit /b %errorlevel%
