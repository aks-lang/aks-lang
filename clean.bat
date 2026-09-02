@echo off
echo Cleaning up build artifacts, object files, and temporary files across project...

:: 1. Remove object files in root and subdirectories
del /f /q *.obj 2>nul
del /f /q src\*.obj 2>nul
del /f /q tests\*.obj 2>nul

:: 2. Remove temporary maps, logs, dumps, traces
del /f /q *.c.map 2>nul
del /f /q tests\*.c.map 2>nul
del /f /q tests\*.ll 2>nul
del /f /q jittrace*.* 2>nul
del /f /q run_out*.txt 2>nul
del /f /q output*.txt 2>nul
del /f /q debug_output*.txt 2>nul
del /f /q temp*.txt 2>nul
del /f /q scratch_*.txt 2>nul
del /f /q scratch_test_regex.* 2>nul
del /f /q retire_dump*.txt 2>nul
del /f /q comp_out.txt 2>nul
del /f /q out.txt 2>nul
del /f /q test_run_output.txt 2>nul
del /f /q test_tcp_out*.txt 2>nul
del /f /q test_trace_out*.txt 2>nul
del /f /q verify_utf8.txt 2>nul
del /f /q akscoredump.json 2>nul
del /f /q build_audit.txt 2>nul
del /f /q build_out.txt 2>nul
del /f /q testSimple_debug.* 2>nul
del /f /q testCollections.* 2>nul
del /f /q test_aot.* 2>nul
del /f /q test_out.wat 2>nul
del /f /q tmp_if_test.aks 2>nul
del /f /q reg_trace.txt 2>nul
del /f /q dap_pause_dump.txt 2>nul
del /f /q compilejit_tr_*.bat 2>nul
del /f /q outtarget.c.map 2>nul
del /f /q *.svg 2>nul
del /f /q tests\*.svg 2>nul
rd /s /q templibrarydir 2>nul
rd /s /q LibraryTestDir 2>nul

:: 3. Remove compiled test executables in tests/ directory
del /f /q tests\*.exe 2>nul
del /f /q tests\*.exp 2>nul
del /f /q tests\*.lib 2>nul

:: 4. Remove residual build files (pdb, ilk, exp)
del /f /q *.ilk 2>nul
del /f /q *.exp 2>nul
del /f /q *.pdb 2>nul
del /f /q vc140.pdb 2>nul

echo Clean complete! Codebase is 100% structured and free of clutter.
