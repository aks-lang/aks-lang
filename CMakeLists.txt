cmake_minimum_required(VERSION 3.16)
project(aks LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(CheckCCompilerFlag)

add_compile_definitions(AKS_RELEASE_BUILD NDEBUG)
if(NOT MSVC)
  add_compile_options(-O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    check_c_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
      add_compile_options(-mavx2)
    endif()
  endif()
else()
  add_compile_options(/O2 /W3)
endif()

set(AKS_COMMON_SRCS
  src/prelude.c
  src/bind.c
  src/dap.c
  src/aot.c
  src/ast.c
  src/compiler.c
  src/cross.c
  src/crypto_custom.c
  src/jitllvm.c
  src/jitqbe.c
  src/jitx64.c
  src/jitarm64.c
  src/learning.c
  src/lexer.c
  src/parser.c
  src/pkg.c
  src/value.c
  src/vm.c
  src/vm_sanitizer.c
  src/stdlib_core.c
  src/stdlib_collections.c
  src/stdlib_math_simd.c
  src/stdlib_crypto_db.c
  src/stdlib_autodiff.c
  src/stdlib_os_net.c
  src/typer.c
  src/staticcodegen.c
  src/wasmcodegen.c
  src/nostdcodegen.c
  src/bench.c
  src/direct_linker.c
  src/diagnostic.c
)

add_library(aks_common OBJECT ${AKS_COMMON_SRCS})
target_include_directories(aks_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(aks_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
  set(AKS_SYS_LIBS
    user32 gdi32 shell32 ws2_32 bcrypt crypt32 dbghelp
    cabinet psapi rpcrt4 advapi32 iphlpapi winmm dnsapi pdh winhttp
  )
else()
  find_package(Threads REQUIRED)
  set(AKS_SYS_LIBS m Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_executable(aks src/main.c $<TARGET_OBJECTS:aks_common>)
target_include_directories(aks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aks PRIVATE ${AKS_SYS_LIBS})

add_executable(aksls src/lsp.c $<TARGET_OBJECTS:aks_common>)
target_include_directories(aksls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aksls PRIVATE ${AKS_SYS_LIBS})

add_executable(aksfmt src/aksfmt.c)
target_include_directories(aksfmt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aksfmt PRIVATE ${AKS_SYS_LIBS})

add_executable(akslint src/akslint.c)
target_include_directories(akslint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(akslint PRIVATE ${AKS_SYS_LIBS})

add_library(aks_shared SHARED src/aks.c $<TARGET_OBJECTS:aks_common>)
target_include_directories(aks_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(aks_shared PRIVATE AKS_BUILD_DLL)
set_target_properties(aks_shared PROPERTIES OUTPUT_NAME aks)
target_link_libraries(aks_shared PRIVATE ${AKS_SYS_LIBS})

add_executable(TestProtocols tests/TestProtocols.c)
target_include_directories(TestProtocols PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(TestProtocols PRIVATE ${AKS_SYS_LIBS})
set_target_properties(TestProtocols PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")

enable_testing()
add_test(NAME TestGenerics COMMAND aks test "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestGenerics.aks")
add_test(NAME TestDynTraits COMMAND aks test "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestDynTraits.aks")
add_test(NAME TestCrypto COMMAND aks run "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestCrypto.aks")
add_test(NAME TestProtocols COMMAND TestProtocols WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

