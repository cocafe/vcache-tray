#ifndef __VCACHE_TRAY_H__
#define __VCACHE_TRAY_H__

#include <pthread.h>

#include <libjj/jkey.h>
#include <libjj/list.h>

extern uint32_t nr_cpu;
extern char json_path[PATH_MAX];
extern uint32_t default_prefer;
extern uint32_t restart_svc;
extern uint32_t restart_svc_force;
extern uint32_t autosave;
extern jbuf_t jbuf_usrcfg;
extern struct list_head profiles;
extern struct list_head profiles_reg;
extern pthread_mutex_t profiles_lock;
extern uint32_t g_should_exit;
extern uint32_t nk_theme;
extern uint32_t cc6_enabled;
extern uint32_t cc1e_enabled;
extern uint32_t pc6_enabled;
extern uint32_t cpb_enabled;
extern uint32_t perf_bias;
extern const char *str_perf_bias[];
extern uint32_t no_tweaks;

#endif // __VCACHE_TRAY_H__