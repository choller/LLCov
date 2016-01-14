#
# LLCov - LLVM Live Coverage instrumentation
# -----------------------------------------
#
# This file was derived from the AFLFuzz project.
#
# Modifications by Christian Holler <choller@mozilla.com>
#
# Original code written by Michal Zalewski <lcamtuf@google.com>
#
# Copyright 2013, 2014, 2015 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at:
#
#   http://www.apache.org/licenses/LICENSE-2.0

PREFIX      ?= /usr/local
HELPER_PATH  = $(PREFIX)/lib/llcov
BIN_PATH     = $(PREFIX)/bin

LLVM_CONFIG ?= llvm-config

CFLAGS      ?= -O2
CFLAGS      += -Wall -D_FORTIFY_SOURCE=2 -g -Wno-pointer-sign \
               -DLLCOV_PATH=\"$(HELPER_PATH)\" -DBIN_PATH=\"$(BIN_PATH)\"

CXXFLAGS    ?= -O2
CXXFLAGS    += -Wall -D_FORTIFY_SOURCE=2 -g -Wno-pointer-sign

CLANG_CFL    = `$(LLVM_CONFIG) --cxxflags` -fno-rtti $(CXXFLAGS)
CLANG_LFL    = `$(LLVM_CONFIG) --ldflags` $(LDFLAGS)

ifeq "$(shell uname)" "Darwin"
CLANG_LFL   += -Wl,-flat_namespace -Wl,-undefined,suppress
endif

ifeq "$(origin CC)" "default"
CC           = clang
CXX          = clang++
endif

PROGS        = llcov-clang llcov-llvm-pass.so llcov-llvm-rt.o

all: test_deps $(PROGS) all_done

test_deps:
	@echo "[*] Checking for working 'llvm-config'..."
	@which $(LLVM_CONFIG) >/dev/null 2>&1 || ( echo "[-] Oops, can't find 'llvm-config'. Install clang or set \$$LLVM_CONFIG or \$$PATH beforehand."; echo "    (Sometimes, the binary will be named llvm-config-3.5 or something like that.)"; exit 1 )
	@echo "[*] Checking for working '$(CC)'..."
	@which $(CC) >/dev/null 2>&1 || ( echo "[-] Oops, can't find '$(CC)'. Make sure that it's in your \$$PATH (or set \$$CC and \$$CXX)."; exit 1 )
	@echo "[+] All set and ready to build."

llcov-clang: llcov-clang.c | test_deps
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)
	ln -sf llcov-clang llcov-clang++

llcov-llvm-pass.so: llcov-llvm-pass.so.cc | test_deps
	$(CXX) $(CLANG_CFL) -shared $< -o $@ $(CLANG_LFL)

llcov-llvm-rt.o: llcov-llvm-rt.o.cc | test_deps
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

all_done: $(PROGS)
	@echo "[+] All done! You can now use 'llcov-clang' to compile programs."

.NOTPARALLEL: clean

clean:
	rm -f *.o *.so *~ a.out core core.[1-9][0-9]*
	rm -f $(PROGS) llcov-clang++
