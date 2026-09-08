#include <glib.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef uint64_t hwaddr;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#include "cleanup-functions.h"
static double run(void (*cleanup)(TextureLayout *), unsigned populated,
                  unsigned iterations)
{
    LARGE_INTEGER a,b,freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&a);
    for (unsigned i=0;i<iterations;i++) {
        TextureLayout *layout=g_new0(TextureLayout,1);
        for(unsigned j=0;j<populated;j++) {
            /* Permutation visits sparse layers/mips before filling all 96. */
            unsigned slot=(j*37)%96;
            layout->layers[slot/16].levels[slot%16].decoded_data=g_malloc(32);
        }
        cleanup(layout);
    }
    QueryPerformanceCounter(&b);
    return (double)(b.QuadPart-a.QuadPart)*1e9/freq.QuadPart/iterations;
}
int main(void)
{
    puts("populated,round,order,old_ns_per_layout,guarded_ns_per_layout");
    cleanup_old(NULL); cleanup_guarded(NULL);
    for(unsigned p=0;p<3;p++) {
        unsigned n=(unsigned[]){1,8,96}[p];
        run(cleanup_old,n,1000);run(cleanup_guarded,n,1000);
        for(unsigned r=0;r<8;r++) {
            double a,b;
            if(r%2){b=run(cleanup_guarded,n,25000);a=run(cleanup_old,n,25000);}
            else {a=run(cleanup_old,n,25000);b=run(cleanup_guarded,n,25000);}
            printf("%u,%u,%s,%.3f,%.3f\n",n,r,r%2?"guarded-first":"old-first",a,b);
        }
    }
    return 0;
}
