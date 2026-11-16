#!/usr/bin/env bash
set -e

echo "Cleaning build artifacts, object files, and temporary residues..."

rm -f *.o src/*.o tests/*.o *.obj src/*.obj tests/*.obj
rm -f *.c.map tests/*.c.map tests/*.ll
rm -f jittrace* run_out*.txt output*.txt debug_output*.txt temp*.txt scratch_*.txt
rm -f retire_dump*.txt comp_out.txt out.txt test_run_output.txt test_tcp_out*.txt
rm -f test_trace_out*.txt verify_utf8.txt akscoredump.json build_audit.txt build_out.txt
rm -f testSimple_debug.* testCollections.* test_aot.* test_out.wat tmp_if_test.aks
rm -f reg_trace.txt dap_pause_dump.txt compilejit_tr_*.bat outtarget.c.map *.svg tests/*.svg
rm -rf templibrarydir LibraryTestDir .aks_cache .akscache obj
rm -f tests/*.exe tests/*.exp tests/*.lib tests/*.dll tests/TestProtocols
rm -f test_loop.exe NativeBenchmarkProof.exe NativeBenchmarkProof NativePiProof IndustryBenchmarksNative
rm -f aks-installer*.exe aks-linux-x64 libaks.so libaks.dylib aks aksls aksfmt akslint
rm -f *.ilk *.exp *.pdb vc140.pdb

echo "Clean complete. Repository is structured and clutter-free."
