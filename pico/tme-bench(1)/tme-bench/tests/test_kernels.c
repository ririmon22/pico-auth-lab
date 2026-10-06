/* Check full-line writes, bounds, and permutation cycle correctness. */
#define main benchmark_main
#include "../tme_bench.c"
#undef main
#include <assert.h>
int main(void) {
    unsigned char *raw, *buf;
    size_t n = 65536, i, j;
    uintptr_t p, start;
    unsigned char *seen = calloc(n/LINE,1);
    assert(seen);
    assert(posix_memalign((void **)&raw,64,n+128) == 0);
    buf = raw+64;
    memset(raw,0x23,n+128);
    cached_stream(buf,n); fence();
    for (i=0;i<n;i++) assert(buf[i] == 255);
    for (i=0;i<64;i++) assert(raw[i] == 0x23 && buf[n+i] == 0x23);
    memset(buf,0,n);
    nt_stream(buf,n); fence();
    for (i=0;i<n;i++) assert(buf[i] == 255);
    for (i=0;i<64;i++) assert(raw[i] == 0x23 && buf[n+i] == 0x23);
    read_stream(buf,n); flush_lines(buf,n);
    p = start = build_chain(buf,n,12345);
    for (i=0;i<n/LINE;i++) {
        assert(p >= (uintptr_t)buf && p < (uintptr_t)(buf+n));
        assert((p-(uintptr_t)buf)%LINE == 0);
        j = (p-(uintptr_t)buf)/LINE;
        assert(!seen[j]); seen[j] = 1;
        p = chase(p,1);
    }
    assert(p == start);
    assert(chase(start,n/LINE) == start);
    memset(seen,0,n/LINE);
    for (i=1;i<=10;i++) {
        p = start = build_chain(buf,n,i);
        for (j=0;j<n/LINE;j++) p = chase(p,1);
        assert(p == start);
    }
    free(seen); free(raw);
    {
        double v[] = {4,1,3,2}, one[] = {7};
        struct stats st = summarize(v,4), single = summarize(one,1);
        assert(fabs(st.mean-2.5)<1e-12 && fabs(st.median-2.5)<1e-12);
        assert(st.min==1 && st.max==4 && st.p95==4);
        assert(fabs(st.sd_population-sqrt(1.25))<1e-12);
        assert(fabs(st.sd_sample-sqrt(5.0/3.0))<1e-12);
        assert(single.sd_population==0 && isnan(single.sd_sample));
    }
    puts("Kernel/statistics tests passed: full-line writes, canaries, single-cycle permutation");
    return 0;
}
