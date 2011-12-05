#include <stdio.h>
#include <stdint.h>

void llvm_llcov_block_call(const char* filename, uint32_t line) {
	printf("Block executed in file %s, line %u\n", filename, line);
}
