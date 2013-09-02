#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static int sockfd = 0;

extern "C" void llvm_llcov_block_call(const char* funcname, const char* filename, uint32_t line, uint32_t relblock) 
	__attribute__((visibility("default")));

extern "C" void llvm_llcov_block_call(const char* funcname, const char* filename, uint32_t line, uint32_t relblock) {
    if (getenv("LLCOV_ABORT")) {
	    fprintf(stderr, "Assertion failure: LLCov: Block executed in file %s, line %u (function %s, line-relative block %u)\n", filename, line, funcname, relblock);
        abort();
    } else if (sockfd > 0) {
        
    } else if (getenv("LLCOV_STDERR")) {
        fprintf(stderr, "file:%s line:%u function:%s relblock:%u\n", filename, line, funcname, relblock);
    } else if (sockfd == 0) {
        char* llcov_host = getenv("LLCOV_HOST");
        if (llcov_host) {
                struct sockaddr_in pin;
                struct hostent *hp;
                
                if ((hp = gethostbyname(llcov_host)) == 0) {
                    perror("gethostbyname");
                    sockfd = -1;
                    return;
                }

                memset(&pin, 0, sizeof(pin));
                pin.sin_family = AF_INET;
                pin.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
                pin.sin_port = htons(7777);

                if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                    perror("socket");
                    sockfd = -1;
                    return;
                }

                if (connect(sockfd,(struct sockaddr *)  &pin, sizeof(pin)) == -1) {
                    perror("connect");
                    sockfd = -1;
                    return;
                }
        } else {
            sockfd = -1;
        }
    }
}
