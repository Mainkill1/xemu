#include <stdint.h>
#include <unistd.h>
volatile unsigned sink;
__attribute__((noinline)) void pgraph_vk_finish(void *p, unsigned n) { sink += n; }
__attribute__((noinline)) void consumer(void) { for (unsigned i=0;i<4;i++) { pgraph_vk_finish(0,3); pgraph_vk_finish(0,11); } }
int main(void) { consumer(); return 0; }
