#ifndef __VCACHE_TRAY_MSR_H__
#define __VCACHE_TRAY_MSR_H__

enum {
        PERF_BIAS_DEFAULT = 0, // ignore perf bias
        PERF_BIAS_NONE,
        PERF_BIAS_CB23,
        PERF_BIAS_GB3,
        PERF_BIAS_RANDX,
        NUM_PERF_BIAS,
};

int perf_bias_set(uint32_t bias);
int package_c6_get(void);
int package_c6_set(int enable);
int core_c6_get(int cpu);
int core_c6_set(int cpu, int enable);
int cpb_get(int cpu);
int cpb_set(int cpu, int enable);

#endif // __VCACHE_TRAY_MSR_H__