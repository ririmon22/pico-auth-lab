#define _GNU_SOURCE
#include <cpuid.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#if !defined(__x86_64__)
#error "Linux x86-64 required"
#endif
#define MIB 1048576ULL
#define LINE 64
#define CHASE_BATCH 65536ULL
static volatile uintptr_t sink;

static void die(const char *s) { perror(s); exit(EXIT_FAILURE); }
static double now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) die("clock_gettime");
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
/* MFENCE waits for previous stores' architectural visibility; it does NOT
 * prove that every dirty cache line has physically reached DRAM.
 * RDTSCP on BOTH boundaries supplies AUX. LFENCE prevents following work
 * from starting early. memory clobbers are compiler barriers. */
static uint64_t stamp(unsigned *aux) {
    unsigned lo, hi, a;
    __asm__ volatile("mfence\n\tlfence\n\trdtscp\n\tlfence"
                     : "=a"(lo), "=d"(hi), "=c"(a) : : "memory");
    *aux = a;
    return ((uint64_t)hi << 32) | lo;
}
static void fence(void) { __asm__ volatile("mfence" : : : "memory"); }
static void flush_lines(void *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i += LINE)
        __asm__ volatile("clflush (%0)" : : "r"((char *)p + i) : "memory");
    fence();
}

/* Read every byte of each cache line. Assembly is volatile; loads cannot be
 * deleted even if an XOR checksum happens to cancel. Four independent chains. */
__attribute__((noinline)) static void read_stream(void *buf, size_t size) {
    char *p = buf;
    size_t n = size / LINE;
    uint64_t checksum;
    __asm__ volatile(
        "pxor %%xmm0,%%xmm0\n\tpxor %%xmm1,%%xmm1\n\t"
        "pxor %%xmm2,%%xmm2\n\tpxor %%xmm3,%%xmm3\n\t"
        "1:\n\t"
        "pxor 0(%[p]),%%xmm0\n\tpxor 16(%[p]),%%xmm1\n\t"
        "pxor 32(%[p]),%%xmm2\n\tpxor 48(%[p]),%%xmm3\n\t"
        "add $64,%[p]\n\tdec %[n]\n\tjnz 1b\n\t"
        "pxor %%xmm1,%%xmm0\n\tpxor %%xmm3,%%xmm2\n\t"
        "pxor %%xmm2,%%xmm0\n\tmovq %%xmm0,%[out]"
        : [p] "+r"(p), [n] "+r"(n), [out] "=r"(checksum)
        : : "xmm0", "xmm1", "xmm2", "xmm3", "cc", "memory");
    sink ^= (uintptr_t)checksum;
}
/* Full-line writes reduce partial-line write-combining effects. */
__attribute__((noinline)) static void cached_stream(void *buf, size_t size) {
    char *p = buf;
    size_t n = size / LINE;
    __asm__ volatile(
        "pcmpeqd %%xmm0,%%xmm0\n\t1:\n\t"
        "movdqa %%xmm0,0(%[p])\n\tmovdqa %%xmm0,16(%[p])\n\t"
        "movdqa %%xmm0,32(%[p])\n\tmovdqa %%xmm0,48(%[p])\n\t"
        "add $64,%[p]\n\tdec %[n]\n\tjnz 1b"
        : [p] "+r"(p), [n] "+r"(n) : : "xmm0", "cc", "memory");
}
__attribute__((noinline)) static void nt_stream(void *buf, size_t size) {
    char *p = buf;
    size_t n = size / LINE;
    __asm__ volatile(
        "pcmpeqd %%xmm0,%%xmm0\n\t1:\n\t"
        "movntdq %%xmm0,0(%[p])\n\tmovntdq %%xmm0,16(%[p])\n\t"
        "movntdq %%xmm0,32(%[p])\n\tmovntdq %%xmm0,48(%[p])\n\t"
        "add $64,%[p]\n\tdec %[n]\n\tjnz 1b\n\tsfence"
        : [p] "+r"(p), [n] "+r"(n) : : "xmm0", "cc", "memory");
}
/* One dependent load per line; next address depends on the previous result.
 * Loop/translation/preemption costs remain part of the observed latency. */
__attribute__((noinline)) static uintptr_t chase(uintptr_t p, uint64_t n) {
    __asm__ volatile("1:\n\tmov (%[p]),%[p]\n\tdec %[n]\n\tjnz 1b"
        : [p] "+r"(p), [n] "+r"(n) : : "cc", "memory");
    return p;
}
static uint64_t rng(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return x * UINT64_C(2685821657736338717);
}
/* Rejection sampling avoids modulo bias in Fisher-Yates. */
static uint64_t bounded(uint64_t *s, uint64_t n) {
    uint64_t x, lim = (uint64_t)(-n) % n;
    do { x = rng(s); } while (x < lim);
    return x % n;
}
static uintptr_t build_chain(void *buf, size_t size, uint64_t seed) {
    size_t count = size / LINE, i, j, tmp;
    size_t *order = malloc(count * sizeof(*order));
    if (!order) die("permutation allocation");
    for (i = 0; i < count; ++i) order[i] = i;
    for (i = count - 1; i > 0; --i) {
        j = (size_t)bounded(&seed, i + 1);
        tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
    for (i = 0; i < count; ++i) {
        uintptr_t next = (uintptr_t)((char *)buf + order[(i + 1) % count] * LINE);
        memcpy((char *)buf + order[i] * LINE, &next, sizeof(next));
    }
    tmp = order[0]; free(order);
    return (uintptr_t)((char *)buf + tmp * LINE);
}
static uint64_t integer(const char *s, uint64_t lo, uint64_t hi) {
    char *end; unsigned long long v;
    errno = 0;
    if (!s[0] || s[0] == '-') goto invalid;
    v = strtoull(s, &end, 10);
    if (errno || *end || v < lo || v > hi) goto invalid;
    return (uint64_t)v;
invalid:
    fprintf(stderr, "Invalid integer: %s (range %" PRIu64 "..%" PRIu64 ")\n", s, lo, hi);
    exit(2);
}
static double seconds(const char *s) {
    char *end; double v;
    errno = 0; v = strtod(s, &end);
    if (errno || end == s || *end || !isfinite(v) || v < .01 || v > 3600) {
        fprintf(stderr, "Duration must be 0.01..3600 seconds\n"); exit(2);
    }
    return v;
}
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
/* Descriptive statistics of valid trial metrics. Sample SD is undefined for n=1.
 * Sorting is in place; p95 uses the nearest-rank convention. */
struct stats {
    double mean, median, min, max, p95, sd_population, sd_sample, cv_percent;
};
static struct stats summarize(double *v, int n) {
    struct stats out;
    double sum = 0, squares = 0;
    int i;
    qsort(v,(size_t)n,sizeof(*v),cmp_double);
    for (i=0;i<n;++i) sum += v[i];
    out.mean = sum / n;
    for (i=0;i<n;++i) squares += (v[i]-out.mean)*(v[i]-out.mean);
    out.median = n%2 ? v[n/2] : (v[n/2-1]+v[n/2])/2;
    out.min = v[0]; out.max = v[n-1]; out.p95 = v[(int)ceil(.95*n)-1];
    out.sd_population = sqrt(squares/n);
    out.sd_sample = n>1 ? sqrt(squares/(n-1)) : NAN;
    out.cv_percent = out.mean ? 100*out.sd_population/fabs(out.mean) : NAN;
    return out;
}
/* a+ preserves existing bytes. A nonblocking advisory lock refuses concurrent
 * writers using this program. Header and final newline are validated first. */
static FILE *open_append_csv(const char *path, const char *legacy, int *extended, int *empty) {
    FILE *f = fopen(path,"a+");
    char header[2048], expected[2048];
    long length;
    if (!f) die("open append CSV");
    if (flock(fileno(f),LOCK_EX|LOCK_NB)) die("CSV already in use");
    if (fseek(f,0,SEEK_END)) die("CSV seek");
    length = ftell(f); if (length<0) die("CSV length");
    *empty = length==0; *extended = 1;
    if (!*empty) {
        rewind(f);
        if (!fgets(header,sizeof(header),f)) die("read CSV header");
        header[strcspn(header,"\r\n")] = 0;
        snprintf(expected,sizeof(expected),"%s,execution_id,started_at_utc",legacy);
        if (!strcmp(header,legacy)) *extended = 0;
        else if (strcmp(header,expected)) {
            fprintf(stderr,"Incompatible CSV header: %s; original data left unchanged\n",path); exit(2);
        }
        if (fseek(f,-1,SEEK_END)) die("CSV tail seek");
        if (fgetc(f)!='\n') {
            fprintf(stderr,"CSV has incomplete final line: %s; original data left unchanged\n",path); exit(2);
        }
    }
    if (fseek(f,0,SEEK_END)) die("CSV append seek");
    return f;
}
static void finish_row(FILE *f, int extended, const char *id, const char *started) {
    if (extended) fprintf(f,",%s,%s",id,started);
    fputc('\n',f);
    if (fflush(f)) die("flush CSV");
}
static void usage(const char *p) {
    printf("Usage: %s -c CPU [-s MiB] [-r RUNS] [-t SECONDS] [-o CSV]\n"
           "  --label off|on|unknown  --test all|read|cached|nt|latency\n"
           "  --seed INTEGER  --thp never|advise  --lock  --drain\n"
           "Defaults: size=512, runs=31, seconds=1, seed=12345, THP=never\n"
           "--drain: cached write + CLFLUSH per pass (includes flush cost)\n"
           "-o CSV also writes CSV.summary.csv; existing compatible files are appended\n", p);
}
int main(int argc, char **argv) {
    int cpu = -1, runs = 31, drain = 0, lock_mem = 0, huge = 0, opt, k, r, failed_sets = 0;
    uint64_t mib = 512, seed = 12345;
    double duration = 1;
    const char *label = "unknown", *test = "all", *path = NULL;
    const char *names[] = {"read", "cached", "nt", "latency"};
    size_t size; void *buf; FILE *csv = NULL, *summary_csv = NULL;
    char *summary_path = NULL;
    int raw_extended = 1, summary_extended = 1, raw_empty, summary_empty;
    char execution_id[96], started_at[48];
    struct timespec realtime; struct tm utc;
    if (clock_gettime(CLOCK_REALTIME,&realtime)) die("realtime");
    if (!gmtime_r(&realtime.tv_sec,&utc)) die("UTC conversion");
    if (!strftime(started_at,sizeof(started_at),"%Y-%m-%dT%H:%M:%SZ",&utc)) {
        fprintf(stderr,"UTC formatting failed\n"); return 2;
    }
    snprintf(execution_id,sizeof(execution_id),"%lld-%09ld-%ld",
             (long long)realtime.tv_sec,realtime.tv_nsec,(long)getpid());
    unsigned a, b, c, d; cpu_set_t allowed, set;
    static const struct option opts[] = {
        {"cpu",1,0,'c'}, {"size",1,0,'s'}, {"runs",1,0,'r'},
        {"seconds",1,0,'t'}, {"output",1,0,'o'}, {"label",1,0,1000},
        {"test",1,0,1001}, {"seed",1,0,1002}, {"thp",1,0,1003},
        {"lock",0,0,1004}, {"drain",0,0,1005}, {"help",0,0,'h'}, {0,0,0,0}
    };
    while ((opt = getopt_long(argc, argv, "c:s:r:t:o:h", opts, NULL)) != -1) {
        switch (opt) {
        case 'c': cpu = (int)integer(optarg,0,CPU_SETSIZE-1); break;
        case 's': mib = integer(optarg,1,SIZE_MAX / MIB); break;
        case 'r': runs = (int)integer(optarg,1,10000); break;
        case 't': duration = seconds(optarg); break;
        case 'o': path = optarg; break;
        case 1000: label = optarg; break;
        case 1001: test = optarg; break;
        case 1002: seed = integer(optarg,1,UINT64_MAX); break;
        case 1003:
            if (!strcmp(optarg,"never")) huge = 0;
            else if (!strcmp(optarg,"advise")) huge = 1;
            else { fprintf(stderr,"Bad THP option\n"); return 2; }
            break;
        case 1004: lock_mem = 1; break;
        case 1005: drain = 1; break;
        case 'h': usage(argv[0]); return 0;
        default: return 2;
        }
    }
    if (cpu < 0 || optind != argc) { usage(argv[0]); return 2; }
    if (strcmp(label,"off") && strcmp(label,"on") && strcmp(label,"unknown")) {
        fprintf(stderr,"label must be off, on, or unknown (user-declared, not detected)\n"); return 2;
    }
    if (strcmp(test,"all") && strcmp(test,"read") && strcmp(test,"cached") &&
        strcmp(test,"nt") && strcmp(test,"latency")) { fprintf(stderr,"Bad test\n"); return 2; }
    if (!__get_cpuid(0x80000001U,&a,&b,&c,&d) || !(d & (1U<<27))) {
        fprintf(stderr,"RDTSCP required\n"); return 2;
    }
    __cpuid(1,a,b,c,d);
    if (!(d & (1U<<26)) || !(d & (1U<<19))) { fprintf(stderr,"SSE2/CLFLUSH required\n"); return 2; }
    if (((b >> 8) & 255U) * 8U != LINE) { fprintf(stderr,"64-byte cache lines required\n"); return 2; }
    if (c & (1U<<31)) fprintf(stderr,"WARNING: virtual machine; results are not bare-metal TME evidence\n");
    if (!__get_cpuid(0x80000007U,&a,&b,&c,&d) || !(d & (1U<<8)))
        fprintf(stderr,"WARNING: invariant TSC not advertised; interpret ticks cautiously\n");
    if (sched_getaffinity(0,sizeof(allowed),&allowed)) die("get affinity");
    if (!CPU_ISSET(cpu,&allowed)) { fprintf(stderr,"Requested CPU is outside allowed affinity\n"); return 2; }
    CPU_ZERO(&set); CPU_SET(cpu,&set);
    if (sched_setaffinity(0,sizeof(set),&set)) die("pin CPU");
    size = (size_t)mib * MIB;
    buf = mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (buf == MAP_FAILED) die("mmap");
    if (madvise(buf,size,huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE)) die("madvise THP");
    /* Full initialization AFTER pinning: first-touch allocation, prefaulting. */
    memset(buf,0x5a,size);
    if (lock_mem && mlock(buf,size)) die("mlock (check memlock limit)");
    if (path) {
        size_t path_len = strlen(path);
        summary_path = malloc(path_len + sizeof(".summary.csv"));
        if (!summary_path) die("summary path allocation");
        memcpy(summary_path,path,path_len);
        memcpy(summary_path+path_len,".summary.csv",sizeof(".summary.csv"));
        csv = open_append_csv(path,"label,test,cpu,size_mib,runs,target_seconds,seed,thp,locked,drain,run,seconds,tsc_ticks,units,metric,tsc_per_unit,aux_start,aux_end,cpu_start,cpu_end,minflt,majflt,nvcsw,nivcsw,valid",&raw_extended,&raw_empty);
        summary_csv = open_append_csv(summary_path,"label,test,cpu,size_mib,target_seconds,seed,thp,locked,drain,total_runs,valid_runs,invalid_runs,metric_unit,mean,median,min,max,p95,sd_population,sd_sample,cv_percent,mean_seconds,mean_tsc_per_unit,tsc_unit,total_minflt,total_majflt,total_nvcsw,total_nivcsw,mean_throughput_mib_s,mean_latency_ns_load",&summary_extended,&summary_empty);
        /* Validate both files before writing either header. */
        if (raw_empty) fprintf(csv,"label,test,cpu,size_mib,runs,target_seconds,seed,thp,locked,drain,run,seconds,tsc_ticks,units,metric,tsc_per_unit,aux_start,aux_end,cpu_start,cpu_end,minflt,majflt,nvcsw,nivcsw,valid,execution_id,started_at_utc\n");
        if (summary_empty) fprintf(summary_csv,"label,test,cpu,size_mib,target_seconds,seed,thp,locked,drain,total_runs,valid_runs,invalid_runs,metric_unit,mean,median,min,max,p95,sd_population,sd_sample,cv_percent,mean_seconds,mean_tsc_per_unit,tsc_unit,total_minflt,total_majflt,total_nvcsw,total_nivcsw,mean_throughput_mib_s,mean_latency_ns_load,execution_id,started_at_utc\n");
        if (!raw_extended || !summary_extended)
            fprintf(stderr,"NOTE: appending legacy CSV schema; execution IDs absent in legacy file\n");

    }
    printf("TME bench v1.2 | GNU C99 / SSE2 | CPU=%d | size=%" PRIu64 " MiB\n"
           "runs=%d, target=%.3fs/sample, label=%s (DECLARED), THP=%s, lock=%d\n"
           "Compiler: %s | TSC values are reference ticks, NOT core cycles\n",
           cpu,mib,runs,duration,label,huge?"advise":"never",lock_mem,__VERSION__);
    printf("Execution ID: %s | started UTC: %s\n",execution_id,started_at);
    if (size < 256 * MIB) fprintf(stderr,"NOTE: small working set may fit LLC; check actual CPU cache size\n");
    for (k = 0; k < 4; ++k) {
        uintptr_t p = 0; double *values; int count = 0;
        double seconds_sum = 0, tsc_unit_sum = 0;
        long minflt_sum = 0, majflt_sum = 0, nvcsw_sum = 0, nivcsw_sum = 0;
        const char *name = (k == 1 && drain) ? "cached_flush" : names[k];
        if (strcmp(test,"all") && strcmp(test,names[k])) continue;
        if (k == 3) p = build_chain(buf,size,seed);
        /* Cold boundary outside measurement; CLFLUSH is portable to old Core.
         * Warmup then establishes the measured steady-state regime. */
        flush_lines(buf,size);
        {
            double warm = now();
            do {
                if (k == 0) read_stream(buf,size);
                else if (k == 1) { cached_stream(buf,size); if (drain) flush_lines(buf,size); }
                else if (k == 2) nt_stream(buf,size);
                else p = chase(p,CHASE_BATCH);
            } while (now() - warm < .2);
            fence();
        }
        values = malloc((size_t)runs * sizeof(*values));
        if (!values) die("samples allocation");
        memset(values,0,(size_t)runs * sizeof(*values));
        /* Resolve clock/getcpu/TSC helper paths before accounting faults. */
        { unsigned aux; (void)stamp(&aux); (void)sched_getcpu(); (void)now(); }
        for (r = 0; r < runs; ++r) {
            struct rusage before, after;
            unsigned aux0,aux1; uint64_t t0,t1,units = 0;
            double start,end,elapsed,metric; int cpu0,cpu1,valid;
            if (getrusage(RUSAGE_THREAD,&before)) die("rusage");
            cpu0 = sched_getcpu(); if (cpu0 < 0) die("getcpu");
            start = now(); t0 = stamp(&aux0);
            do {
                if (k == 0) { read_stream(buf,size); units += size; }
                else if (k == 1) { cached_stream(buf,size); if (drain) flush_lines(buf,size); units += size; }
                else if (k == 2) { nt_stream(buf,size); units += size; }
                else { p = chase(p,CHASE_BATCH); units += CHASE_BATCH; }
            } while (now() - start < duration);
            t1 = stamp(&aux1); end = now();
            cpu1 = sched_getcpu(); if (cpu1 < 0) die("getcpu");
            if (getrusage(RUSAGE_THREAD,&after)) die("rusage");
            elapsed = end - start;
            valid = (aux0 == aux1 && cpu0 == cpu && cpu1 == cpu && t1 > t0 &&
                     before.ru_minflt == after.ru_minflt && before.ru_majflt == after.ru_majflt);
            metric = k == 3 ? elapsed * 1e9 / (double)units : (double)units / MIB / elapsed;
            minflt_sum += after.ru_minflt-before.ru_minflt;
            majflt_sum += after.ru_majflt-before.ru_majflt;
            nvcsw_sum += after.ru_nvcsw-before.ru_nvcsw;
            nivcsw_sum += after.ru_nivcsw-before.ru_nivcsw;
            if (valid) {
                values[count++] = metric;
                seconds_sum += elapsed;
                tsc_unit_sum += (double)(t1-t0)/units;
            }
            printf("%-13s %3d %.6fs %12.3f %s | %.4f TSC/%s | faults=%ld/%ld switches=%ld/%ld %s\n",
                   name,r+1,elapsed,metric,k==3?"ns/load":"MiB/s",(double)(t1-t0)/units,
                   k==3?"load":"byte",after.ru_minflt-before.ru_minflt,
                   after.ru_majflt-before.ru_majflt,after.ru_nvcsw-before.ru_nvcsw,
                   after.ru_nivcsw-before.ru_nivcsw,valid?"OK":"INVALID");
            if (csv) { fprintf(csv,
                "%s,%s,%d,%" PRIu64 ",%d,%.6f,%" PRIu64 ",%s,%d,%d,%d,%.9f,%" PRIu64 ",%" PRIu64 ",%.9f,%.9f,%u,%u,%d,%d,%ld,%ld,%ld,%ld,%d",
                label,name,cpu,mib,runs,duration,seed,huge?"advise":"never",lock_mem,drain,
                r+1,elapsed,t1-t0,units,metric,(double)(t1-t0)/units,aux0,aux1,cpu0,cpu1,
                after.ru_minflt-before.ru_minflt,after.ru_majflt-before.ru_majflt,
                after.ru_nvcsw-before.ru_nvcsw,after.ru_nivcsw-before.ru_nivcsw,valid);
                finish_row(csv,raw_extended,execution_id,started_at);
            }
        }
        if (count) {
            struct stats st = summarize(values,count);
            const char *unit = k==3 ? "ns/load" : "MiB/s";
            printf("SUMMARY %s valid=%d/%d mean=%.3f median=%.3f min=%.3f max=%.3f p95=%.3f sd_pop=%.3f sd_sample=%.3f CV=%.3f%% %s\n\n",
                   name,count,runs,st.mean,st.median,st.min,st.max,st.p95,
                   st.sd_population,st.sd_sample,st.cv_percent,unit);
            if (summary_csv) {
                fprintf(summary_csv,"%s,%s,%d,%" PRIu64 ",%.6f,%" PRIu64 ",%s,%d,%d,%d,%d,%d,%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,",
                        label,name,cpu,mib,duration,seed,huge?"advise":"never",lock_mem,drain,
                        runs,count,runs-count,unit,st.mean,st.median,st.min,st.max,st.p95,st.sd_population);
                if (count>1) fprintf(summary_csv,"%.9f",st.sd_sample);
                fprintf(summary_csv,",%.9f,%.9f,%.9f,%s,%ld,%ld,%ld,%ld,",
                        st.cv_percent,seconds_sum/count,tsc_unit_sum/count,k==3?"ticks/load":"ticks/byte",
                        minflt_sum,majflt_sum,nvcsw_sum,nivcsw_sum);
                if (k==3) fprintf(summary_csv,",%.9f",st.mean);
                else fprintf(summary_csv,"%.9f,",st.mean);
                finish_row(summary_csv,summary_extended,execution_id,started_at);
            }
        } else {
            printf("SUMMARY %s: NO VALID SAMPLES; rerun after diagnosing faults/migration\n",name);
            if (summary_csv) {
                fprintf(summary_csv,"%s,%s,%d,%" PRIu64 ",%.6f,%" PRIu64 ",%s,%d,%d,%d,0,%d,%s",
                        label,name,cpu,mib,duration,seed,huge?"advise":"never",lock_mem,drain,
                        runs,runs,k==3?"ns/load":"MiB/s");
                /* 10 undefined fields: mean through mean_tsc_per_unit. */
                for (r=0;r<10;++r) fputc(',',summary_csv);
                fprintf(summary_csv,",%s,%ld,%ld,%ld,%ld,,",k==3?"ticks/load":"ticks/byte",
                        minflt_sum,majflt_sum,nvcsw_sum,nivcsw_sum);
                finish_row(summary_csv,summary_extended,execution_id,started_at);
            }
            failed_sets = 1;
        }
        sink ^= p; free(values);
    }
    printf("sink=%" PRIuPTR "\n",sink);
    if (csv && fclose(csv)) die("close CSV");
    if (summary_csv && fclose(summary_csv)) die("close summary CSV");
    if (summary_path) { printf("Summary CSV: %s\n",summary_path); free(summary_path); }
    if (lock_mem && munlock(buf,size)) die("munlock");
    if (munmap(buf,size)) die("munmap");
    return failed_sets ? 1 : 0;
}
