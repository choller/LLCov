#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static FILE* filefd = NULL;

inline __attribute__((always_inline))
void writeData(const char* funcname, const char* filename, uint32_t line, uint32_t relblock) {
    fprintf(filefd, "file:%s line:%u relblock:%u\n", filename, line, relblock);
    fflush(filefd);
}

extern "C" void llvm_llcov_block_call(const char* funcname, const char* filename, uint32_t line, uint32_t relblock) 
	__attribute__((visibility("default")));

extern "C" void llvm_llcov_block_call(const char* funcname, const char* filename, uint32_t line, uint32_t relblock) {
    if (filefd != NULL) {
        writeData(funcname, filename, line, relblock);
    } else if (getenv("LLCOV_ABORT")) {
        fprintf(stderr, "Assertion failure: LLCov: Block executed in file %s, line %u (function %s, line-relative block %u)\n", filename, line, funcname, relblock);
        abort();
    } else if (getenv("LLCOV_STDERR")) {
        fprintf(stderr, "file:%s line:%u func:%s relblock:%u\n", filename, line, funcname, relblock);
    } else if (getenv("LLCOV_FILE")) {
        filefd = fopen(getenv("LLCOV_FILE"), "a");
        if (filefd != NULL) {
            writeData(funcname, filename, line, relblock);
        }
    }
}
