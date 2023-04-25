#ifndef __VCACHE_TRAY_MSR_H__
#define __VCACHE_TRAY_MSR_H__

int package_c6_get(void);
int package_c6_set(int enable);
int core_c6_get(int cpu);
int core_c6_set(int cpu, int enable);
int cpb_get(int cpu);
int cpb_set(int cpu, int enable);

#endif // __VCACHE_TRAY_MSR_H__