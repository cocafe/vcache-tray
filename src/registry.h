#ifndef __VCACHE_TRAY_REGISTRY_H__
#define __VCACHE_TRAY_REGISTRY_H__

#include <libjj/list.h>

#define REG_KEY_PREFERENCES     L"SYSTEM\\CurrentControlSet\\Services\\amd3dvcache\\Preferences"
#define REG_VAL_DEFAULT_TYPE    L"DefaultType"

#define REG_KEY_APP             L"SYSTEM\\CurrentControlSet\\Services\\amd3dvcache\\Preferences\\App"
#define REG_VAL_ENDSWITH        L"EndsWith"
#define REG_VAL_PREFER_TYPE     L"Type"

enum {
        PREFER_FREQ = 0,
        PREFER_CACHE,
        NUM_PREFER_OPTS,
        INVALID_PREFER_TYPE,
};

int default_prefer_registry_read(uint32_t *ret);
int __default_prefer_registry_write(uint32_t val);
int default_prefer_registry_write(uint32_t val);
int profiles_registry_read(struct list_head *head);
int profiles_registry_clean(void);
int __profiles_registry_write(struct list_head *head);
int profiles_registry_write(struct list_head *head);

#endif // __VCACHE_TRAY_REGISTRY_H__