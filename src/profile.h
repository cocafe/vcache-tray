#ifndef __VCACHE_TRAY_PROFILE_H__
#define __VCACHE_TRAY_PROFILE_H__

typedef struct profile profile_t;

struct profile {
        struct list_head node;

        wchar_t name[PATH_MAX];
        wchar_t process[PATH_MAX];
        uint32_t prefer;
};


#endif // __VCACHE_TRAY_PROFILE_H__