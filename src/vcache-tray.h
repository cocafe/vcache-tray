#ifndef __VCACHE_TRAY_H__
#define __VCACHE_TRAY_H__

#include <pthread.h>

#include <libjj/jkey.h>
#include <libjj/list.h>

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

#endif // __VCACHE_TRAY_H__