#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <set>
#include <tuple>

static FILE* filefd = NULL;

inline __attribute__((always_inline))
void writeData(const char* funcname, const char* filename, uint32_t line, uint32_t relblock) {
    static std::set< std::tuple<uint32_t, uint32_t, std::string> > seen;
    std::tuple<uint32_t, uint32_t, std::string> tup(line, relblock, std::string(filename));
    if (seen.find(tup) == seen.end()) {
        fprintf(filefd, "file:%s line:%u relblock:%u\n", filename, line, relblock);
        /*fwrite(funcname, strlen(funcname)+1, 1, filefd);
        fwrite(filename, strlen(filename)+1, 1, filefd);
        fwrite(&line, sizeof(uint32_t), 1, filefd);
        fwrite(&relblock, sizeof(uint32_t), 1, filefd);*/
        fflush(filefd);
        seen.insert(tup);
    }
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
