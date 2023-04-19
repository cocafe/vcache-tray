#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <windows.h>
#include <processthreadsapi.h>
#include <tlhelp32.h>

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

extern HANDLE process_snapshot_create(void);
extern int process_snapshot_iterate(HANDLE snapshot, int (*cb)(PROCESSENTRY32 *, va_list arg), ...);

char json_path[PATH_MAX] = DEFAULT_JSON_PATH;
uint32_t default_prefer = PREFER_CACHE;
uint32_t restart_svc = 0;
uint32_t restart_svc_force = 0;
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
                        jbuf_bool_add(b, "restart_service_force", &restart_svc_force);
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

static void wnd_msg_process(int blocking)
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

static void loaded_profiles_name_generate(void)
{
        profile_t *p, *s;

        list_for_each_entry_safe(p, s, &profiles, node) {
                if (profile_name_generate(p) == -EINVAL) {
                        pr_mb_warn("\"%ls\" does not contains \".exe\", removed\n", p->process);
                        list_del(&p->node);
                        free(p);
                }
        }
}

static void loaded_profiles_validate(void)
{
        profile_t *p, *s;

        list_for_each_entry_safe(p, s, &profiles, node) {
                profile_t *pp, *ps, *tail = container_of(profiles.prev, profile_t, node);

                if (is_strptr_not_set(p->name)) {
                        pr_mb_warn("Unable to create registry key with empty profile name, removed\n");
                        list_del(&p->node);
                        free(p);
                        continue;
                }

                if (p != tail) {
                        list_for_each_entry_safe(pp, ps, &p->node, node) {
                                if (is_wstr_equal(p->name, pp->name)) {
                                        pr_mb_warn("Unable to create duplicated registry key for profile \"%ls\", removed\n", pp->name);
                                        list_del(&pp->node);
                                        free(pp);
                                }
                        }
                }
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

        if ((err = usrcfg_init())) {
                if (err == -EINVAL) {
                        pr_mb_err("invalid JSON config, please check\n");
                        return err;
                }

                mb_info("failed to load config, using default configs and registry profiles\n");
        }

        if ((err = default_prefer_registry_read(NULL))) {
                // TODO: check amd3dv cache process...
                pr_mb_err("failed to read %ls, are amd3dv driver and service installed properly?\n", REG_KEY_PREFERENCES);
                goto exit_usrcfg;
        }

        profiles_registry_read(&profiles_reg);
        profiles_merge(&profiles, &profiles_reg);
        profile_list_free(&profiles_reg);
        loaded_profiles_name_generate();
        loaded_profiles_validate();
        __profiles_registry_write(&profiles);
        default_prefer_registry_write(default_prefer);
        default_prefer_registry_read(&default_prefer);

        profile_gui_init();

        if ((err = vcache_tray_init(ins))) {
                pr_mb_err("failed to init tray\n");
                goto exit_gui;
        }

        wnd_msg_process(1);

        vcache_tray_exit();

        if (autosave) {
                if ((err = jbuf_save(&jbuf_usrcfg, json_path)))
                        pr_mb_err("failed to save config, err = %d\n", err);
        }

exit_gui:
        profile_gui_deinit();

exit_usrcfg:
        usrcfg_exit();

        logging_exit();

        return err;
}
