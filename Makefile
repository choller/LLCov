#===- lib/llcov/Makefile.old -------------------------------*- Makefile -*--===#
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

OS=$(shell uname | tr '[A-Z]' '[a-z]')
ROOT=$(shell pwd)
MAKEFILE=Makefile  # this file.

ifeq ($(ARCH), android)
  ANDROID_CFLAGS= \
		-DANDROID \
		-D__WORDSIZE=32 \
		-I$(ANDROID_BUILD_TOP)/external/stlport/stlport \
		-I$(ANDROID_BUILD_TOP)/bionic \
		-I$(ANDROID_BUILD_TOP)/bionic/libstdc++/include \
		-I$(ANDROID_BUILD_TOP)/bionic/libc/arch-arm/include \
		-I$(ANDROID_BUILD_TOP)/bionic/libc/include \
		-I$(ANDROID_BUILD_TOP)/bionic/libc/kernel/common \
		-I$(ANDROID_BUILD_TOP)/bionic/libc/kernel/arch-arm \
		-I$(ANDROID_BUILD_TOP)/bionic/libm/include \
		-I$(ANDROID_BUILD_TOP)/bionic/libm/include/arm \
		-I$(ANDROID_BUILD_TOP)/bionic/libthread_db/include \
		-L$(ANDROID_PRODUCT_OUT)/obj/lib
  CLANG_FLAGS= \
		-ccc-host-triple arm-linux-androideabi \
		-D__compiler_offsetof=__builtin_offsetof \
		-D__ELF__=1 \
		-ccc-gcc-name arm-linux-androideabi-g++ \
		$(ANDROID_CFLAGS)
  CC=$(ANDROID_EABI_TOOLCHAIN)/arm-linux-androideabi-gcc $(ANDROID_CFLAGS)
  CXX=$(ANDROID_EABI_TOOLCHAIN)/arm-linux-androideabi-g++ $(ANDROID_CFLAGS)
endif

ifeq ($(ARCH), arm)
  # Example make command line:
  # CROSSTOOL=$HOME/x-tools/arm-unknown-linux-gnueabi/ PATH=$CROSSTOOL/bin:$PATH make ARCH=arm asan_test
  CLANG_FLAGS= \
		-ccc-host-triple arm-unknown-linux-gnueabi \
		-march=armv7-a -mfloat-abi=softfp -mfp=neon \
		-ccc-gcc-name arm-unknown-linux-gnueabi-g++ \
		-B$(CROSSTOOL)/lib/gcc/arm-unknown-linux-gnueabi/4.4.4 \
		-B$(CROSSTOOL)/arm-unknown-linux-gnueabi/sys-root/usr/lib \
		-I$(CROSSTOOL)/lib/gcc/arm-unknown-linux-gnueabi/4.4.4/include \
		-I$(CROSSTOOL)/arm-unknown-linux-gnueabi/include/c++/4.4.4 \
		-I$(CROSSTOOL)/arm-unknown-linux-gnueabi/include/c++/4.4.4/arm-unknown-linux-gnueabi \
		-I$(CROSSTOOL)/arm-unknown-linux-gnueabi/sys-root/include \
		-I$(CROSSTOOL)/arm-unknown-linux-gnueabi/sys-root/usr/include \
		-L$(CROSSTOOL)/lib/gcc/arm-unknown-linux-gnueabi/4.4.4 \
		-L$(CROSSTOOL)/arm-unknown-linux-gnueabi/sys-root/lib \
		-L$(CROSSTOOL)/arm-unknown-linux-gnueabi/sys-root/usr/lib
  CC=$(CROSSTOOL)/bin/arm-unknown-linux-gnueabi-gcc 
  CXX=$(CROSSTOOL)/bin/arm-unknown-linux-gnueabi-g++
endif

CLANG_FLAGS=
LLVM_BUILD=$(LLVM_ROOT)/build/Release+Asserts
CLANG_CC=$(LLVM_BUILD)/bin/clang $(CLANG_FLAGS)
CLANG_CXX=$(LLVM_BUILD)/bin/clang++ $(CLANG_FLAGS)

CC=$(CLANG_CC)
CXX=$(CLANG_CXX)

CFLAGS:=-Wall -fvisibility=hidden

CLEANROOM_CXX=$(CXX) -Wall

BIN=bin_$(OS)

LIBS=
ARCH=i386
IS_X86_64 := $(shell echo __x86_64__ | ${CC} -E -xc - | tail -n 1)
ifeq (${IS_X86_64}, 1)
ARCH=x86_64
endif

ifeq ($(ARCH), i386)
BITS=32
SUFF=$(BITS)
CFLAGS:=$(CFLAGS) -m$(BITS)
endif

ifeq ($(ARCH), x86_64)
BITS=64
SUFF=$(BITS)
CFLAGS:=$(CFLAGS) -m$(BITS)
endif

ifeq ($(ARCH), arm)
BITS=32
SUFF=_arm
CFLAGS:=$(CFLAGS) -march=armv7-a
endif

ifeq ($(ARCH), android)
BITS=32
SUFF=_android
CFLAGS:=$(CFLAGS)
endif

PIE=

LIBLLCOV_INST_DIR=$(LLVM_BUILD)/lib/clang/$(OS)/$(ARCH)
LIBLLCOV_A=$(LIBLLCOV_INST_DIR)/libclang_rt.llcov.a

LIBLLCOV_OBJ=$(BIN)/llcov_print$(SUFF).o

all: lib

lib64:
	$(MAKE) $(MAKEFILE) ARCH=x86_64 lib
lib32:
	$(MAKE) $(MAKEFILE) ARCH=i386 lib

$(BIN):
	mkdir -p $(BIN)

$(BIN)/%$(SUFF).o: %.cc $(MAKEFILE)
	$(CXX) $(PIE) $(CFLAGS) -std=c++0x -fPIC -c -O2 -fno-exceptions -o $@ -g $< $(LLCOV_FLAGS)

$(BIN)/%$(SUFF).o: %.c $(MAKEFILE)
	$(CC) $(PIE) $(CFLAGS) -fPIC -c -O2 -o $@ -g $< $(LLCOV_FLAGS)

ifeq ($(OS),darwin)
LD_FLAGS=-framework Foundation
else
LD_FLAGS=
endif

lib: $(LIBLLCOV_A)

test: $(LIBLLCOV_OBJ)

LLCOV_CPP=LLCov.cpp
LLCOV_CPP_INST_DIR=$(LLVM_ROOT)/lib/Transforms/Instrumentation/
srcinstall: $(LLCOV_CPP)
	cp $(LLCOV_CPP) $(LLCOV_CPP_INST_DIR)

$(LIBLLCOV_A): $(BIN) $(LIBLLCOV_OBJ) $(MAKEFILE)
	mkdir -p $(LIBLLCOV_INST_DIR)
	ar ru $@ $(LIBLLCOV_OBJ)
	$(CXX) -shared $(CFLAGS) $(LIBLLCOV_OBJ) $(LD_FLAGS) -o $(BIN)/libllcov$(SUFF).so

clean:
	rm -f *.o *.ll *.S *.a a.out
	rm -rf $(BIN)
