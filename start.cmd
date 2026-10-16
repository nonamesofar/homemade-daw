@echo off
rem Build and start Sampler. Usage: start.cmd [-Config release] [-NoBuild] [app args]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start.ps1" %*
