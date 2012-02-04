#include <stdio.h>
#include <stdint.h>

extern "C" void llvm_llcov_block_call(const char* funcname, const char* filename, uint32_t line) 
	__attribute__((visibility("default")));

extern "C" void llvm_llcov_block_call(const char* funcname, const char* filename, uint32_t line) {
	fprintf(stderr, "Block executed in file %s, line %u (function %s)\n", filename, line, funcname);
}
