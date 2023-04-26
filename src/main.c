#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>

#include <windows.h>
#include <processthreadsapi.h>
#include <tlhelp32.h>
#include <sysinfoapi.h>

#include <libjj/utils.h>
#include <libjj/opts.h>
#include <libjj/jkey.h>
#include <libjj/iconv.h>
#include <libjj/logging.h>

#include <libwinring0/winring0.h>

#include "msr.h"
#include "registry.h"
#include "profile.h"
#include "tray.h"
#include "vcache-tray.h"

#define DEFAULT_JSON_PATH               "config.json"

static const char *str_prefer[] = {
        [PREFER_CACHE] = "cache",
        [PREFER_FREQ] = "freq"
};

extern HANDLE process_snapshot_create(void);
extern int process_snapshot_iterate(HANDLE snapshot, int (*cb)(PROCESSENTRY32 *, va_list arg), ...);

uint32_t nr_cpu;

char json_path[PATH_MAX] = DEFAULT_JSON_PATH;
uint32_t default_prefer = PREFER_CACHE;
uint32_t cc6_enabled = 0;
uint32_t pc6_enabled = 0;
uint32_t cpb_enabled = 1;
uint32_t restart_svc = 0;
uint32_t restart_svc_force = 0;
uint32_t autosave = 0;
LIST_HEAD(profiles);

LIST_HEAD(profiles_reg);
jbuf_t jbuf_usrcfg;
pthread_mutex_t profiles_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
uint32_t g_should_exit = 0;

lsopt_strbuf(c, json_path, json_path, sizeof(json_path), "JSON config path");

static uint32_t nr_cpu_get(void)
{
        SYSTEM_INFO info = { 0 };

        GetSystemInfo(&info);

        return info.dwNumberOfProcessors;
}

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
                        jbuf_u32_add(b, "nk_theme", &nk_theme);
                }

                jbuf_obj_close(b, settings);
        }

        {
                void *tweaks = jbuf_obj_open(b, "tweaks");

                {
                        jbuf_bool_add(b, "pkg_c6_enabled", &pc6_enabled);
                        jbuf_bool_add(b, "core_c6_enabled", &cc6_enabled);
                        jbuf_bool_add(b, "cpb_enabled", &cpb_enabled);
                }

                jbuf_obj_close(b, tweaks);
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

int usrcfg_save(void)
{
        int err;

        pthread_mutex_lock(&profiles_lock);

        if ((err = jbuf_save(&jbuf_usrcfg, json_path)))
                pr_mb_err("failed to save config to \"%s\", err = %d\n", json_path, err);

        pr_info("saved json config: \"%s\"\n", json_path);

        pthread_mutex_unlock(&profiles_lock);

        return err;
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

                if (msg.message == WM_QUIT) {
                        g_should_exit = 1;
                        break;
                }

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

static int is_process_exist(PROCESSENTRY32 *pe32, va_list arg)
{
        wchar_t *str = va_arg(arg, wchar_t *);

        if (is_wstr_equal(pe32->szExeFile, str)) {
                return 1;
        }

        return 0;
}

static int is_amd3dv_service_running(void)
{
        wchar_t svc[] = L"amd3dvcacheSvc.exe";
        wchar_t user[] = L"amd3dvcacheUser.exe";
        HANDLE snapshot = process_snapshot_create();
        int ret = 0;

        if (!snapshot) {
                pr_mb_err("failed to get process snapshot\n");
                return 0;
        }

        if (process_snapshot_iterate(snapshot, is_process_exist, svc) == 1 &&
            process_snapshot_iterate(snapshot, is_process_exist, user) == 1) {
                ret = 1;
        }

        CloseHandle(snapshot);

        return ret;
}

static int json_path_fix(void)
{
        wchar_t json_path_w[_MAX_PATH] = { 0 };
        wchar_t full_path[4096] = { 0 };
        size_t cnt;

        iconv_utf82wc(json_path, sizeof(json_path), json_path_w, sizeof(json_path_w));
        json_path_w[WCBUF_LEN(json_path_w) - 1] = L'\0';

        cnt = GetFullPathName(json_path_w, WCBUF_LEN(json_path_w), full_path, NULL);
        if (cnt == 0) {
                mb_err("failed to get full path of json config\n");
                return -EIO;
        }

        iconv_wc2utf8(full_path, sizeof(full_path), json_path, sizeof(json_path));

        pr_rawlvl(INFO, "json config: \"%s\"\n", json_path);

        return 0;
}

static void msr_apply(void)
{
        for (uint32_t cpu = 0; cpu < nr_cpu; cpu++) {
                core_c6_set(cpu, cc6_enabled);
                cpb_set(cpu, cpb_enabled);
        }

        package_c6_set(pc6_enabled);
}

static LRESULT CALLBACK powernotify_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
        if (msg == WM_POWERBROADCAST && wparam == PBT_APMRESUMEAUTOMATIC) {
                pr_info("system has resumed\n");
                msr_apply();

                return TRUE;
        }

        return DefWindowProc(hwnd, msg, wparam, lparam);
}

static HANDLE power_notify_wnd_create(void)
{
        HWND wnd = NULL;
        WNDCLASSEX wc = { 0 };

        wc.cbSize              = sizeof(WNDCLASSEX);
        wc.lpfnWndProc         = powernotify_proc;
        wc.hInstance           = GetModuleHandle(NULL);
        wc.lpszClassName       = L"PowerNotifyWnd";
        if (!RegisterClassEx(&wc)) {
                pr_err("RegisterClassEx() failed\n");
                return NULL;
        }

        wnd = CreateWindowEx(0, L"PowerNotifyWnd",
                             NULL, 0, 0, 0, 0, 0, 0, 0,
                             GetModuleHandle(NULL), 0);
        if (wnd == NULL) {
                pr_err("CreateWindowEx() failed\n");
                return NULL;
        }

        ShowWindow(wnd, SW_HIDE);
        UpdateWindow(wnd);

        return wnd;
}

int WINAPI wWinMain(HINSTANCE ins, HINSTANCE prev_ins,
        LPWSTR cmdline, int cmdshow)
{
        HANDLE pwrnotify_wnd, pwrnotify;
        int err;

        setbuf(stdout, NULL);

        // this equals "System(enhanced)" in compatibility setting
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);

        nr_cpu = nr_cpu_get();

        if ((err = lopts_parse(__argc, __wargv, NULL)))
                return err;

        if ((err = logging_init()))
                return err;

        if ((err = json_path_fix()))
                return err;

        if ((err = winring0_init())) {
                mb_err("Failed to initialize WinRing0");
                return err;
        }

        pwrnotify_wnd = power_notify_wnd_create();
        pwrnotify = RegisterSuspendResumeNotification(pwrnotify_wnd, DEVICE_NOTIFY_WINDOW_HANDLE);

        if ((err = usrcfg_init())) {
                if (err == -EINVAL) {
                        pr_mb_err("Invalid JSON config, please check\n");
                        return err;
                }

                mb_info("Failed to load config: \"%s\"\nUsing default values and registry profiles\n", json_path);
        }

        if (!is_amd3dv_service_running()) {
                mb_err("AMD 3D V-Cache service is not running, are driver installed and bios configured properly?\n");
                goto exit_usrcfg;
        }

        if ((err = default_prefer_registry_read(NULL))) {
                mb_err("Failed to read \"%ls\"\nAre amd3dv driver and service installed properly?\n", REG_KEY_PREFERENCES);
                goto exit_usrcfg;
        }

        msr_apply();

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

        UnregisterPowerSettingNotification(pwrnotify);

        vcache_tray_exit();

        if (autosave) {
                usrcfg_save();
        }

exit_gui:
        profile_gui_deinit();

exit_usrcfg:
        usrcfg_exit();

        winring0_deinit();

        logging_exit();

        return err;
}
