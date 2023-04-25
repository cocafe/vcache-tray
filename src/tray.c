#include <stdint.h>

#include <windows.h>
#include <sysinfoapi.h>

#include <libjj/tray.h>
#include <libjj/logging.h>

#include <resource.h>

#include "msr.h"
#include "registry.h"
#include "profile.h"
#include "vcache-tray.h"

extern int usrcfg_save(void);
extern struct tray_menu nk_theme_menus[];

static uint32_t nr_cpu;

static void tray_icon_update(void);

static void tray_default_prefer_update(struct tray_menu *m)
{
        uint32_t val = (uint32_t){ (uint64_t)m->userdata };

        if (default_prefer == val)
                m->checked = 1;
        else
                m->checked = 0;
}

static void tray_default_prefer_click(struct tray_menu *m)
{
        uint32_t val = (uint32_t){ (uint64_t)m->userdata };

        default_prefer_registry_write(val);
        default_prefer_registry_read(&default_prefer);
        tray_icon_update();
}

void tray_option_item_update(struct tray_menu *m)
{
        uint32_t *var = m->userdata;
        uint32_t value = (intptr_t)m->userdata2;

        if (*var == value)
                m->checked = 1;
        else
                m->checked = 0;
}

void tray_option_item_click(struct tray_menu *m)
{
        uint32_t *var = m->userdata;
        uint32_t value = (intptr_t)m->userdata2;

        *var = value;
}

static void tray_bool_item_update(struct tray_menu *m)
{
        uint32_t *val = m->userdata;

        if (val && *val) {
                m->checked = 1;
        } else {
                m->checked = 0;
        }
}

static void tray_bool_item_click(struct tray_menu *m)
{
        uint32_t *val = m->userdata;

        if (val) {
                *val = !*val;
        }
}

static void console_show_click(struct tray_menu *m)
{
        if (!g_console_alloc)
                return;

        m->checked = !m->checked;

        if (m->checked) {
                g_console_show = 1;
                console_show(1);

                pr_raw("====================================================================\n");
                pr_raw("=== CLOSE THIS LOGGING WINDOW WILL TERMINATE PROGRAM, ^C TO HIDE ===\n");
                pr_raw("====================================================================\n");

                return;
        }

        g_console_show = 0;
        console_hide();
}

static void console_show_update(struct tray_menu *m)
{
        if (!g_console_alloc) {
                m->checked = 0;
                m->disabled = 1;
                return;
        }

        if (is_console_hid())
                m->checked = 0;
        else
                m->checked = 1;
}

static void loglvl_click(struct tray_menu *m)
{
        uint32_t level = (size_t)m->userdata;

        m->checked = !m->checked;

        if (m->checked) {
                g_logprint_level |= level;
        } else {
                g_logprint_level &= ~level;
        }
}

static void loglvl_update(struct tray_menu *m)
{
        uint32_t level = (size_t)m->userdata;

        if (g_logprint_level & level)
                m->checked = 1;
        else
                m->checked = 0;
}

static void tray_save_click(struct tray_menu *m)
{
        usrcfg_save();
}

static void tray_profile_click(struct tray_menu *m)
{
        profile_gui_show();
}

static void tray_exit_click(struct tray_menu *m)
{
        PostQuitMessage(0);
}

static void tray_restart_svc_click(struct tray_menu *m)
{
        if (amd3dv_service_restart())
                mb_err("failed to restart AMD 3D V-CACHE service");
}

static void tray_tweak_menu_update(struct tray_menu *m, int (*get)(int))
{
        int enabled, cpu;

        if (!get) {
                m->disabled = 1;
                return;
        }

        cpu = 0;
        enabled = get(cpu);
        if (enabled < 0) {
                m->disabled = 1;
                return;
        }

        m->disabled = 0;
        if (enabled)
                m->checked = 1;
        else
                m->checked = 0;
}

static void tray_tweak_menu_on_click(struct tray_menu *m, int (*set)(int, int))
{
        if (!set)
                return;

        for (int cpu = 0; cpu < (int)nr_cpu; cpu++) {
                int ret = set(cpu, !m->checked);
                if (ret < 0) {
                        mb_err("Failed to write CPU%d", cpu);
                        return;
                }
        }
}

static void tray_pkgc6_update(struct tray_menu *m)
{
        int enabled = package_c6_get();

        if (enabled < 0) {
                m->disabled = 1;
                return;
        }

        m->disabled = 0;
        if (enabled)
                m->checked = 1;
        else
                m->checked = 0;
}

static void tray_pkgc6_on_click(struct tray_menu *m)
{
        package_c6_set(!m->checked);
}

static void tray_cc6_update(struct tray_menu *m)
{
        tray_tweak_menu_update(m, core_c6_get);
}

static void tray_cc6_on_click(struct tray_menu *m)
{
        tray_tweak_menu_on_click(m, core_c6_set);
}

static void tray_cpb_update(struct tray_menu *m)
{
        tray_tweak_menu_update(m, cpb_get);
}

static void tray_cpb_on_click(struct tray_menu *m)
{
        tray_tweak_menu_on_click(m, cpb_set);
}

static void tray_double_click(struct tray *tray, void *userdata)
{
        profile_gui_show();
}

static struct tray g_tray = {
        .lbtn_dblclick = tray_double_click,
        .icon = {
                .path = NULL,
                .id = IDI_APP_ICON,
        },
        .menu = (struct tray_menu[]) {
                {
                        .name = L"Prefer CCD",
                        .submenu = (struct tray_menu[]) {
                                { .name = L"3D V-Cache", .pre_show = tray_default_prefer_update, .on_click = tray_default_prefer_click, .userdata=(void *)PREFER_CACHE },
                                { .name = L"Frequency", .pre_show = tray_default_prefer_update, .on_click = tray_default_prefer_click, .userdata=(void *)PREFER_FREQ },
                                { .is_end = 1 },
                        },
                },
                {
                        .name = L"Tweak",
                        .submenu = (struct tray_menu[]) {
                                { .name = L"Core C6", .pre_show = tray_cc6_update, .on_click = tray_cc6_on_click },
                                { .name = L"Package C6", .pre_show = tray_pkgc6_update, .on_click = tray_pkgc6_on_click },
                                { .name = L"Core Performance Boost", .pre_show = tray_cpb_update, .on_click = tray_cpb_on_click },
                                { .is_end = 1 },
                        },
                },
                { .is_separator = 1 },
                { .name = L"Profiles", .on_click = tray_profile_click, },
                { .name = L"Save", .on_click = tray_save_click },
                { .is_separator = 1 },
                {
                        .name = L"Logging",
                        .submenu = (struct tray_menu[]) {
                                { .name = L"Show", .pre_show = console_show_update, .on_click = console_show_click },
                                { .is_separator = 1 },
                                { .name = L"Verbose", .pre_show = loglvl_update, .on_click = loglvl_click, .userdata = (void *)LOG_LEVEL_VERBOSE },
                                { .name = L"Debug",   .pre_show = loglvl_update, .on_click = loglvl_click, .userdata = (void *)LOG_LEVEL_DEBUG   },
                                { .name = L"Info",    .pre_show = loglvl_update, .on_click = loglvl_click, .userdata = (void *)LOG_LEVEL_INFO    },
                                { .name = L"Notice",  .pre_show = loglvl_update, .on_click = loglvl_click, .userdata = (void *)LOG_LEVEL_NOTICE  },
                                { .name = L"Warning", .pre_show = loglvl_update, .on_click = loglvl_click, .userdata = (void *)LOG_LEVEL_WARN    },
                                { .name = L"Error",   .pre_show = loglvl_update, .on_click = loglvl_click, .userdata = (void *)LOG_LEVEL_ERROR   },
                                { .name = L"Fatal",   .pre_show = loglvl_update, .on_click = loglvl_click, .userdata = (void *)LOG_LEVEL_FATAL   },
                                { .is_end = 1 },
                        },
                },
                {
                        .name = L"Settings",
                        .submenu = (struct tray_menu[]) {
                                { .name = L"Theme", .submenu = nk_theme_menus },
                                { .is_separator = 1 },
                                { .name = L"Restart Service Now", .on_click = tray_restart_svc_click },
                                { .is_separator = 1 },
                                { .name = L"Restart AMD3DV Service on Apply", .pre_show = tray_bool_item_update, .on_click = tray_bool_item_click, .userdata=&restart_svc },
                                { .name = L"Restart Service Forcefully", .pre_show = tray_bool_item_update, .on_click = tray_bool_item_click, .userdata=&restart_svc_force },
                                { .name = L"Auto Save on Exit", .pre_show = tray_bool_item_update, .on_click = tray_bool_item_click, .userdata=&autosave },
                                { .is_end = 1 },
                        },
                },
                { .is_separator = 1 },
                { .name = L"Exit", .on_click = tray_exit_click },
                { .is_end = 1 },
        }
};

void tray_icon_update(void)
{
        switch (default_prefer) {
        case PREFER_CACHE:
                g_tray.icon.id = IDI_APP_ICON_BLUE;
                tray_tooltip_set(&g_tray, L"Prefer Cache");
                break;

        case PREFER_FREQ:
                g_tray.icon.id = IDI_APP_ICON_RED;
                tray_tooltip_set(&g_tray, L"Prefer Frequency");
                break;

        default:
                g_tray.icon.id = IDI_APP_ICON;
                tray_tooltip_set(&g_tray, L"V-Cache Tray");
                break;
        }
}

uint32_t nr_cpu_get(void)
{
        SYSTEM_INFO info = { 0 };

        GetSystemInfo(&info);

        return info.dwNumberOfProcessors;
}

int vcache_tray_init(HINSTANCE ins)
{
        int err;

        nr_cpu = nr_cpu_get();

        err = tray_init(&g_tray, ins);
        if (!err) {
                tray_icon_update();
                tray_update_post(&g_tray);
        }

        return err;
}

void vcache_tray_exit(void)
{
        tray_exit(&g_tray);
}
