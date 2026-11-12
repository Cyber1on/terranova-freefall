@echo off
rem Terra Nova: Freefall's benchmark: a fixed course through mission 3 with your settings,
rem about a minute (Esc stops it). The results are shown at the end and added to
rem tnfree-bench.txt next to tnfree.exe.
start "" "%~dp0tnfree.exe" --bench
