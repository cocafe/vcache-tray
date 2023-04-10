#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <windows.h>

#include <libjj/utils.h>
#include <libjj/opts.h>
#include <libjj/jkey.h>
#include <libjj/tray.h>
#include <libjj/iconv.h>
#include <libjj/logging.h>

#include "registry.h"
#include "profile.h"
#include "tray.h"

#define DEFAULT_JSON_PATH               "config.json"

static const char *str_prefer[] = {
        [PREFER_CACHE] = "cache",
        [PREFER_FREQ] = "freq"
};

extern BOOL SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT value);

char json_path[PATH_MAX] = DEFAULT_JSON_PATH;
uint32_t default_prefer = PREFER_CACHE;
uint32_t restart_svc = 0;
uint32_t autosave = 0;
LIST_HEAD(profiles);
LIST_HEAD(profiles_reg);
jbuf_t jbuf_usrcfg;

lsopt_strbuf(c, json_path, json_path, sizeof(json_path), "JSON config path");

static int usrcfg_root_key_create(jbuf_t *b)
{
        int err;
        void *root;

        if ((err = jbuf_init(b, JBUF_INIT_ALLOC_KEYS))) {
                pr_err("jbuf_init(), err = %d\n", err);
                return err;
        }

        root = jbuf_obj_open(b, NULL);

        {
                void *settings = jbuf_obj_open(b, "settings");

                {
                        jbuf_strval_add(b, "default_prefer", &default_prefer, str_prefer, sizeof(str_prefer));
                        jbuf_bool_add(b, "restart_service_on_apply", &restart_svc);
                        jbuf_bool_add(b, "autosave_on_exit", &autosave);
                }

                jbuf_obj_close(b, settings);
        }

        {
                void *profile_arr = jbuf_list_arr_open(b, "profiles");

                jbuf_list_arr_setup(b, profile_arr, &profiles, sizeof(profile_t), offsetof(profile_t, node), 0, 0);

                {
                        void *arr_obj = jbuf_offset_obj_open(b, NULL, 0);

                        jbuf_offset_wstrbuf_add(b, "name", offsetof(profile_t, name), sizeof(((profile_t *)(0))->name));
                        jbuf_offset_wstrbuf_add(b, "process", offsetof(profile_t, process), sizeof(((profile_t *)(0))->process));
                        jbuf_offset_strval_add(b, "prefer", offsetof(profile_t, prefer), str_prefer, ARRAY_SIZE(str_prefer));

                        jbuf_obj_close(b, arr_obj);
                }

                jbuf_arr_close(b, profile_arr);
        }

        jbuf_obj_close(b, root);

        return 0;
}

static int usrcfg_init(void)
{
        jbuf_t *b = &jbuf_usrcfg;
        int err;

        if ((err = usrcfg_root_key_create(b)))
                return err;

        if ((err = jbuf_load(b, json_path)))
                return err;

        pr_raw("loaded json config:\n");
        jbuf_traverse_print(b);

        return 0;
}

static void usrcfg_exit(void)
{
        jbuf_deinit(&jbuf_usrcfg);
}

static void profiles_name_generate(struct list_head *h)
{
        profile_t *p, *s;

        list_for_each_entry_safe(p, s, h, node) {
                if (is_strptr_set(p->name))
                        continue;

                wchar_t *exe = wcsstr(p->process, L".exe");
                if (!exe) {
                        pr_warn("\"%ls\" does not contains \".exe\", removed\n", p->process);
                        list_del(&p->node);
                        free(p);

                        continue;
                }

                memset(p->name, L'\0', sizeof(p->name));
                wcsncpy(p->name, p->process, ((intptr_t)exe - (intptr_t)p->process) / sizeof(wchar_t));
        }
}

// FIXME: O(n^)
static int is_profile_list_contain(struct list_head *h, profile_t *p)
{
        profile_t *t;

        list_for_each_entry(t, h, node) {
                if (is_wstr_equal(t->name, p->name))
                        return 1;
        }

        return 0;
}

static int profiles_merge(struct list_head *head, struct list_head *append)
{
        profile_t *n, *s;

        list_for_each_entry_safe(n, s, append, node) {
                if (is_profile_list_contain(head, n)) {
                        pr_dbg("profile %ls already exists\n", n->name);
                        continue;
                }

                list_del(&n->node);
                list_add_tail(&n->node, head);
        }

        return 0;
}

static void profiles_free(struct list_head *head)
{
        profile_t *p, *s;

        list_for_each_entry_safe(p, s, head, node) {
                list_del(&p->node);
                free(p);
        }
}

void wnd_msg_process(int blocking)
{
        MSG msg;

        while (1) {
                if (blocking) {
                        GetMessage(&msg, NULL, 0, 0);
                } else {
                        PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
                }

                if (msg.message == WM_QUIT)
                        break;

                TranslateMessage(&msg);
                DispatchMessage(&msg);
        }
}

int WINAPI wWinMain(HINSTANCE ins, HINSTANCE prev_ins,
        LPWSTR cmdline, int cmdshow)
{
        int err;

        setbuf(stdout, NULL);

        // this equals "System(enhanced)" in compatibility setting
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);

        if ((err = lopts_parse(__argc, __wargv, NULL)))
                return err;

        if ((err = logging_init()))
                return err;

        if ((usrcfg_init()))
                pr_err("failed to load user config, create new one\n");

        if ((err = default_prefer_registry_read(NULL))) {
                // TODO: check amd3dv cache process...
                pr_mb_err("failed to read registry, is amd3dv driver and service installed properly?\n");
                goto exit_usrcfg;
        }

        profiles_name_generate(&profiles);
        profiles_registry_read(&profiles_reg);
        profiles_merge(&profiles, &profiles_reg);
        profiles_free(&profiles_reg);
        __profiles_registry_write(&profiles);
        default_prefer_registry_write(default_prefer);
        default_prefer_registry_read(&default_prefer);

        if ((err = vcache_tray_init(ins))) {
                pr_mb_err("failed to init tray\n");
                goto exit_usrcfg;
        }

        wnd_msg_process(1);

        vcache_tray_exit();

        if (autosave) {
                if ((err = jbuf_save(&jbuf_usrcfg, json_path)))
                        pr_mb_err("failed to save config, err = %d\n", err);
        }

exit_usrcfg:
        usrcfg_exit();

        logging_exit();

        return err;
}
