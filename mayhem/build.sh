#!/usr/bin/env bash
#
# mayhem/build.sh — build the x64dbg fuzz harnesses and the ElfBug ELF-image test suite.
#
# Upstream's classic core (src/dbg, src/gui) is Windows-only (MSVC / llvm-mingw); the Linux tree is
# src/cross (Qt, FetchContent). The harnessed code is plain C/C++ with no Qt dependency, so it is
# compiled directly from the upstream sources here:
#   fuzz_elf_image     src/cross/ElfBug/ElfBug/elf/ElfImage.cpp      (ELF headers/sections)
#   fuzz_patternfind   src/dbg/patternfind.cpp                       (find/findall/snr patterns)
#   fuzz_zydis_disasm  src/zydis_wrapper/zydis_wrapper.cpp + Zydis   (instruction decoding layer)
#   fuzz_procfs        src/cross/ElfBug/ElfBug/process/ProcFs.cpp   (/proc maps/stat/status text)
# Test suite: upstream's ElfBug Catch2 suite (tests/test_elf_image.cpp), linked against the whole
# ElfBug library built with normal flags, plus the fixtures it reads.
#
# All objects go to build-mayhem/ (gitignored by upstream's `build*/`), so the build is re-runnable,
# offline, and survives `git clean -ffdX`.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
B="$SRC/build-mayhem"
F="$B/fuzz"      # sanitized + coverage-instrumented objects
T="$B/tests"     # normal-flag test build
mkdir -p "$F" "$T/targets"

ZYDIS_INC=(-I"$SRC/src/zydis_wrapper" -I"$SRC/src/zydis_wrapper/Zydis")
FUZZ_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link"

# 1) The project code under test, sanitized and coverage-instrumented.
$CC  $FUZZ_CFLAGS "${ZYDIS_INC[@]}" -c "$SRC/src/zydis_wrapper/Zydis/Zydis.c" -o "$F/Zydis.o"
$CXX $FUZZ_CFLAGS -std=c++17 "${ZYDIS_INC[@]}" -c "$SRC/src/zydis_wrapper/zydis_wrapper.cpp" -o "$F/zydis_wrapper.o"
$CXX $FUZZ_CFLAGS -std=c++17 -c "$SRC/src/dbg/patternfind.cpp" -o "$F/patternfind.o"
$CXX $FUZZ_CFLAGS -std=c++20 -I"$SRC/src/cross/ElfBug" -c "$SRC/src/cross/ElfBug/ElfBug/elf/ElfImage.cpp" -o "$F/ElfImage.o"
$CXX $FUZZ_CFLAGS -std=c++20 -I"$SRC/src/cross/ElfBug" -c "$SRC/src/cross/ElfBug/ElfBug/process/ProcFs.cpp" -o "$F/ProcFs.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$F/lsan_off.o"
$CC  $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$F/standalone_main.o"

# 2) Harnesses: libFuzzer binary + run-once standalone reproducer each.
link_harness() {
  local name="$1" std="$2"; shift 2
  local inc=() objs=()
  while [ $# -gt 0 ]; do case "$1" in -I*) inc+=("$1");; *) objs+=("$1");; esac; shift; done
  $CXX $FUZZ_CFLAGS "$std" "${inc[@]}" -c "$SRC/mayhem/harnesses/$name.cpp" -o "$F/$name.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$F/$name.o" "${objs[@]}" "$F/lsan_off.o" -o "$SRC/$name"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$F/standalone_main.o" "$F/$name.o" "${objs[@]}" "$F/lsan_off.o" -o "$SRC/$name-standalone"
}
link_harness fuzz_elf_image    -std=c++20 -I"$SRC/src/cross/ElfBug" "$F/ElfImage.o"
link_harness fuzz_patternfind  -std=c++17 -I"$SRC/src/dbg" "$F/patternfind.o"
link_harness fuzz_zydis_disasm -std=c++17 "${ZYDIS_INC[@]}" "$F/zydis_wrapper.o" "$F/Zydis.o"
link_harness fuzz_procfs       -std=c++20 -I"$SRC/src/cross/ElfBug" "$F/ProcFs.o"

# 3) Upstream's ElfBug ELF-image tests with NORMAL flags (no sanitizers): the ElfBug static library
#    (as src/cross/cmake.toml's ElfBug target), the Catch2 test TU and the fixtures it reads.
TFLAGS="-O1 -g $COVERAGE_FLAGS"
objs=()
for src in "$SRC"/src/cross/ElfBug/ElfBug/*/*.cpp; do
  o="$T/$(basename "${src%.cpp}").o"
  $CXX $TFLAGS -std=c++20 -DELFBUG_BUILDING -I"$SRC/src/cross/ElfBug" "${ZYDIS_INC[@]}" -c "$src" -o "$o"
  objs+=("$o")
done
$CC  $TFLAGS "${ZYDIS_INC[@]}" -c "$SRC/src/zydis_wrapper/Zydis/Zydis.c" -o "$T/Zydis.o"
$CXX $TFLAGS -std=c++17 "${ZYDIS_INC[@]}" -c "$SRC/src/zydis_wrapper/zydis_wrapper.cpp" -o "$T/zydis_wrapper.o"
$CXX $TFLAGS -std=c++20 -I"$SRC/src/cross/ElfBug" "${ZYDIS_INC[@]}" \
  -DELFBUG_TESTS_TARGETS_DIR="\"$T/targets\"" \
  -c "$SRC/src/cross/ElfBug/tests/test_elf_image.cpp" -o "$T/test_elf_image.o"
$CXX $TFLAGS "$T/test_elf_image.o" "${objs[@]}" "$T/zydis_wrapper.o" "$T/Zydis.o" \
  $(pkg-config --libs catch2-with-main) -pthread -o "$T/ElfBug_tests"
# Fixtures, with the options src/cross/ElfBug/tests/cmake.toml gives them.
$CXX -g -O0 -fPIE -pie "$SRC/src/cross/ElfBug/tests/targets/hello_elfbug.cpp" -o "$T/targets/hello_elfbug"
$CXX -g -O0 -fPIE -pie "$SRC/src/cross/ElfBug/tests/targets/segfault.cpp" -o "$T/targets/segfault"
$CC  -g -O0 -fno-pie -static -no-pie "$SRC/src/cross/ElfBug/tests/targets/static_target.c" -o "$T/targets/static_target"

ls -l "$SRC"/fuzz_elf_image "$SRC"/fuzz_patternfind "$SRC"/fuzz_zydis_disasm "$SRC"/fuzz_procfs "$T/ElfBug_tests"
