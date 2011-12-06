#include <stdio.h>
#include <stdint.h>

void llvm_llcov_block_call(const char* funcname, const char* filename, uint32_t line) {
	printf("Block executed in file %s, line %u (function %s)\n", filename, line, funcname);
}
