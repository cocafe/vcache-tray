#ifndef __VCACHE_TRAY_PROFILE_H__
#define __VCACHE_TRAY_PROFILE_H__

#define PROFILE_NAME_LEN                (_MAX_FNAME)
#define PROFILE_PROCESS_LEN             (_MAX_FNAME)

typedef struct profile profile_t;

struct profile {
        struct list_head node;

        wchar_t name[PROFILE_NAME_LEN];
        wchar_t process[PROFILE_PROCESS_LEN];
        uint32_t prefer;
};

int is_profile_list_contain(struct list_head *h, profile_t *p);

int profiles_merge(struct list_head *head, struct list_head *append);

profile_t *profile_new(void);
int profile_name_generate(profile_t *p);

void profile_list_free(struct list_head *head);

int profile_gui_show(void);
void profile_gui_init(void);
void profile_gui_deinit(void);

#endif // __VCACHE_TRAY_PROFILE_H__