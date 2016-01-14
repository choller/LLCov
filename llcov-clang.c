/*
  LLCov - LLVM Live Coverage instrumentation
  -----------------------------------------

  Modified by Christian Holler <choller@mozilla.com>

  This wrapper was derived from the LLVM integration of AFLFuzz,

  written by Laszlo Szekeres <lszekeres@google.com> and
             Michal Zalewski <lcamtuf@google.com>

  Copyright 2013, 2014, 2015 Google Inc. All rights reserved.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at:

    http://www.apache.org/licenses/LICENSE-2.0
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "types.h"
#include "debug.h"
#include "alloc-inl.h"

#define VERSION "0.9a"

static u8*  obj_path;               /* Path to runtime libraries         */
static u8** cc_params;              /* Parameters passed to the real CC  */
static u32  cc_par_cnt = 1;         /* Param count, including argv0      */


/* Try to find the runtime libraries. If that fails, abort. */

static void find_obj(u8* argv0) {

  u8 *llcov_path = getenv("LLCOV_PATH");
  u8 *slash, *tmp;

  if (llcov_path) {

    tmp = alloc_printf("%s/llcov-llvm-rt.o", llcov_path);

    if (!access(tmp, R_OK)) {
      obj_path = llcov_path;
      ck_free(tmp);
      return;
    }

    ck_free(tmp);

  }

  slash = strrchr(argv0, '/');

  if (slash) {

    u8 *dir;

    *slash = 0;
    dir = ck_strdup(argv0);
    *slash = '/';

    tmp = alloc_printf("%s/llcov-llvm-rt.o", dir);

    if (!access(tmp, R_OK)) {
      obj_path = dir;
      ck_free(tmp);
      return;
    }

    ck_free(tmp);
    ck_free(dir);

  }

  if (!access(LLCOV_PATH "/llcov-llvm-rt.o", R_OK)) {
    obj_path = LLCOV_PATH;
    return;
  }

  FATAL("Unable to find 'llcov-llvm-rt.o' or 'llcov-llvm-pass.so'. Please set LLCOV_PATH");
 
}


/* Copy argv to cc_params, making the necessary edits. */

static void edit_params(u32 argc, char** argv) {

  u8 x_set = 0, maybe_linking = 1;
  u8 *name;

  cc_params = ck_alloc((argc + 64) * sizeof(u8*));

  name = strrchr(argv[0], '/');
  if (!name) name = argv[0]; else name++;

  if (!strcmp(name, "llcov-clang++")) {
    u8* alt_cxx = getenv("LLCOV_CXX");
    cc_params[0] = alt_cxx ? alt_cxx : (u8*)"clang++";
  } else {
    u8* alt_cc = getenv("LLCOV_CC");
    cc_params[0] = alt_cc ? alt_cc : (u8*)"clang";
  }

  cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = "-load";
  cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = alloc_printf("%s/llcov-llvm-pass.so", obj_path);
  cc_params[cc_par_cnt++] = "-Qunused-arguments";

  while (--argc) {
    u8* cur = *(++argv);

#if defined(__x86_64__)
    if (!strcmp(cur, "-m32")) FATAL("-m32 is not supported");
#endif

    if (!strcmp(cur, "-x")) x_set = 1;

    if (!strcmp(cur, "-c") || !strcmp(cur, "-S") || !strcmp(cur, "-E") ||
        !strcmp(cur, "-v")) maybe_linking = 0;

    cc_params[cc_par_cnt++] = cur;

  }

  /* Debug information is required to properly resolve the original
     locations of the instrumented basic blocks */
  cc_params[cc_par_cnt++] = "-g";

  if (maybe_linking) {

    if (x_set) {
      cc_params[cc_par_cnt++] = "-x";
      cc_params[cc_par_cnt++] = "none";
    }

    cc_params[cc_par_cnt++] = alloc_printf("%s/llcov-llvm-rt.o", obj_path);

  }

  cc_params[cc_par_cnt] = NULL;

}


/* Main entry point */

int main(int argc, char** argv) {

  if (isatty(2) && !getenv("LLCOV_QUIET")) {

    SAYF(cCYA "llcov-clang " cBRI VERSION  cRST " by <choller@mozilla.com>\n");

  }

  if (argc < 2) {

    SAYF("\n"
         "This is a compiler wrapper for LLCOV. It serves as a drop-in replacement\n"
         "for clang, letting you recompile third-party code with the required runtime\n"
         "instrumentation. A common use pattern would be one of the following:\n\n"

         "  CC=%s/llcov-clang ./configure\n"
         "  CXX=%s/llcov-clang++ ./configure\n\n"

         "You can specify custom next-stage toolchain via LLCOV_CC and LLCOV_CXX.\n\n",
         BIN_PATH, BIN_PATH);

    exit(1);

  }


  find_obj(argv[0]);

  edit_params(argc, argv);

  execvp(cc_params[0], (char**)cc_params);

  FATAL("Oops, failed to execute '%s' - check your PATH", cc_params[0]);

  return 0;

}
