#!/usr/bin/env bash
set -euo pipefail

rm -rf .akscache

echo "Building compiler (POSIX)..."
bash build.sh

echo "Running core VM & compiler tests..."
./aks run tests/TestNullPtr.aks
./aks run tests/TestBounds.aks
./aks run --no-bounds-check tests/TestBounds.aks
./aks run tests/TestOverflow.aks
./aks run --strict-overflow tests/TestOverflow.aks >/dev/null 2>&1 || true
./aks run tests/TestUaf.aks
! ./aks run tests/TestTypeError.aks >/dev/null 2>&1
! ./aks run --immutable-by-default tests/TestConstError.aks >/dev/null 2>&1
! ./aks run tests/TestBorrowError.aks >/dev/null 2>&1
! ./aks run tests/TestStaticRecursion.aks >/dev/null 2>&1
./aks run tests/TestOpts.aks
./aks run tests/TestRaii.aks
./aks run tests/SecurityTest.aks
./aks run tests/PlatformTest.aks
./aks run tests/TestCollections.aks
./aks run tests/TestCrypto.aks
./aks run tests/TestSerialization.aks
./aks run tests/TestStringUnicode.aks
./aks run tests/TestNewFeatures.aks
./aks run tests/TestSimdSwizzle.aks
./aks run tests/TestPhysicalUnits.aks
./aks run tests/TestActorModel.aks
./aks run tests/TestAutoDiff.aks
./aks run tests/TestMatrixDualDeepSuite.aks
./aks run tests/TestContractsVerified.aks
./aks run tests/TestAlgebraicEffects.aks
./aks run --jit tests/TestJitHotLoop.aks
./aks run --jit tests/TestJitRegisterHotLoop.aks
./aks run --parallel tests/TestParallelCompilation.aks
./aks run tests/TestReverseModeAutodiff.aks
./aks run tests/TestTensorAutodiff.aks
./aks run tests/TestStructuredConcurrency.aks
./aks run tests/TestSupervisedActors.aks
./aks run tests/TestRegionSafety.aks
./aks run tests/TestComptimeExecution.aks
./aks run --jit tests/TestJitOsrRegAlloc.aks
./aks run tests/TestModuleTopologicalSort.aks
./aks run tests/TestSimdVec8i.aks
./aks run tests/TestFileLock.aks
./aks run tests/TestSystemDisplay.aks
./aks run tests/TestLockFreeQueue.aks
./aks run tests/TestSoftwareTransactionalMemory.aks
./aks run tests/TestScientificOdeSuite.aks
./aks run tests/TestPhase8Enhancements.aks
./aks run tests/TestSlicingAndRawStrings.aks
./aks run tests/TestLoopDestructuringAndPathPkg.aks
./aks test tests/TestLoopDestructuringAndPathPkg.aks
./aks run tests/TestFastBmi2.aks
./aks run tests/TestSimdStringMatch.aks
./aks run tests/TestSparseFile.aks
./aks run tests/TestHardLink.aks
./aks run tests/TestEntropyPool.aks
./aks run tests/TestReadWriteLock.aks
./aks run tests/TestThreadBarrier.aks
./aks run tests/TestConditionVariable.aks
./aks run tests/TestRawDnsResolver.aks
./aks run tests/TestThreadPoolWork.aks
./aks run tests/TestThreadPoolTimer.aks
./aks run tests/TestNativeNetworkingConcurrencySuite.aks
./aks run tests/TestLanguageImprovements.aks
./aks test tests/TestLanguageImprovements.aks
./aks run tests/TestGenerics.aks
./aks test tests/TestGenerics.aks
./aks run tests/TestComptimeTypelevel.aks
./aks test tests/TestComptimeTypelevel.aks
./aks run tests/TestSqlite.aks
./aks test tests/TestSqlite.aks
./aks run tests/TestFnPatternDestructuring.aks
./aks test tests/TestFnPatternDestructuring.aks
./aks run tests/TestCHeaderImport.aks
test ! -e tests/c_math_helpers.h.aks
./aks compile-exe tests/TestCHeaderImport.aks -o test_cheader_aot
./test_cheader_aot
rm -f test_cheader_aot
./aks compile-llvm tests/TestCHeaderImport.aks -o test_cheader.ll
rm -f test_cheader.ll
test ! -e tests/c_math_helpers.h.aks
./aks run tests/TestDynTraits.aks
./aks test tests/TestDynTraits.aks
./aks run --sanitize=address,thread tests/TestSanitizers.aks
if ./aks run --no-bounds-check --sanitize=address tests/TestFailSanitizerOob.aks 2>/dev/null; then
  echo "FAILED: ASan OOB unexpectedly succeeded"
  exit 1
fi
if ./aks run --no-borrow-check --sanitize=address tests/TestFailSanitizerUaf.aks 2>/dev/null; then
  echo "FAILED: ASan UAF unexpectedly succeeded"
  exit 1
fi
if ./aks run --sanitize=thread tests/TestFailSanitizerRace.aks 2>/dev/null; then
  echo "FAILED: TSan race unexpectedly succeeded"
  exit 1
fi

./aks compile-exe tests/TestComptimeExecution.aks -o test_standalone
./test_standalone
rm -f test_standalone

echo "Running Cross-Backend Differential Verification..."
./aks run tests/TestDifferentialSuite.aks > tests/diff_vm.out
./aks run --jit tests/TestDifferentialSuite.aks > tests/diff_jit.out
./aks run --parallel tests/TestDifferentialSuite.aks > tests/diff_parallel.out
cmp -s tests/diff_vm.out tests/diff_jit.out
cmp -s tests/diff_vm.out tests/diff_parallel.out
rm -f tests/diff_vm.out tests/diff_jit.out tests/diff_parallel.out
./aks compile-mlir tests/TestDifferentialSuite.aks tests/TestDifferentialSuite.mlir
test -f tests/TestDifferentialSuite.mlir
rm -f tests/TestDifferentialSuite.mlir
./aks compile-llvm tests/TestDifferentialSuite.aks tests/TestDifferentialSuite.ll
test -f tests/TestDifferentialSuite.ll
rm -f tests/TestDifferentialSuite.ll

echo "Running strict type tests..."
./aks run --strict-types tests/TestStrict.aks
! ./aks run --strict-types tests/TestStrictFail.aks >/dev/null 2>&1
./aks run --strict-types tests/TestAdvancedTypes.aks
./aks run --incremental tests/TestIncrementalTyping.aks
! ./aks run --strict-types tests/TestFailOpaque.aks >/dev/null 2>&1
! ./aks run --strict-types tests/TestFailRefine.aks >/dev/null 2>&1
! ./aks run --strict-types tests/TestFailArray.aks >/dev/null 2>&1
! ./aks run --strict-types tests/TestFailEffect.aks >/dev/null 2>&1
./aks run --strict-types tests/TestDynTraits.aks
! ./aks run --strict-types tests/TestFailDynTrait.aks >/dev/null 2>&1
./aks run --strict-types tests/TestErrorRecovery.aks >/dev/null 2>&1 || true

echo "Running package manager tests..."
rm -rf mylib myapp mypathlib mypathapp outtarget.c outtarget.c.map
./aks pkg init mylib >/dev/null
printf '{ "name": "mylib", "version": "1.0.0", "entrypoint": "src/main.aks", "dependencies": {} }\n' > mylib/aks.json
printf 'print("Hello from mylib");\n' > mylib/src/main.aks
(cd mylib && ../aks pkg publish >/dev/null)
./aks pkg init myapp >/dev/null
printf '{ "name": "myapp", "version": "1.0.0", "entrypoint": "src/main.aks", "dependencies": {} }\n' > myapp/aks.json
(cd myapp && ../aks pkg add mylib@^1.0.0 >/dev/null)
(cd myapp && ../aks pkg install >/dev/null)
test -f myapp/aks.lock
(cd myapp && ../aks pkg verify >/dev/null)
printf 'import "mylib";\nprint("Hello from myapp");\n' > myapp/src/main.aks
(cd myapp && ../aks pkg run)
(cd myapp && ../aks pkg build --bundle >/dev/null)
test -f myapp/dist/myapp.aks
./aks run myapp/dist/myapp.aks
./aks compile-aot myapp/dist/myapp.aks outtarget.c --target c-code >/dev/null
test -f outtarget.c
rm -rf mylib myapp outtarget.c outtarget.c.map
rm -rf mypathlib mypathapp
./aks pkg init mypathlib >/dev/null
printf '{ "name": "mypathlib", "version": "1.0.0", "entrypoint": "src/main.aks", "dependencies": {} }\n' > mypathlib/aks.json
printf 'print("Hello from mypathlib");\n' > mypathlib/src/main.aks
./aks pkg init mypathapp >/dev/null
printf '{ "name": "mypathapp", "version": "1.0.0", "entrypoint": "src/main.aks", "dependencies": {} }\n' > mypathapp/aks.json
(cd mypathapp && ../aks pkg add path:../mypathlib >/dev/null)
(cd mypathapp && ../aks pkg install >/dev/null)
(cd mypathapp && ../aks pkg verify >/dev/null)
rm -rf mypathlib mypathapp

echo "Running CLI tools tests..."
printf 'print("toolstest ok");\n' > sample_temp.aks
./aks run sample_temp.aks
./aks compile sample_temp.aks -o sample_temp.aksb
./aks run-bin sample_temp.aksb
./aks highlight sample_temp.aks sample_temp.html
printf 'fn greet() { print("hi"); }\n' > sample_doc.aks
./aks doc sample_doc.aks sample_doc.md
./aks explain "undefined variable" >/dev/null
./aks help >/dev/null
rm -f sample_temp.* sample_doc.*

echo "Running Editor Protocol tests (LSP and DAP)..."
./tests/TestProtocols

rm -f akscoredump.json jittrace* compilejit_* jitloop* *.svg tests/*.svg
rm -rf LibraryTestDir templibrarydir

echo "All POSIX tests passed successfully!"

