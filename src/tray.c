#include <stdint.h>

#include <libjj/tray.h>
#include <libjj/logging.h>

#include <resource.h>

#include "registry.h"
#include "vcache-tray.h"

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
        int err;
        if ((err = jbuf_save(&jbuf_usrcfg, json_path)))
                pr_mb_err("failed to save config, err = %d\n", err);
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

static struct tray g_tray = {
        .icon = {
                .path = NULL,
                .id = IDI_APP_ICON,
        },
        .menu = (struct tray_menu[]) {
                {
                        .name = L"Prefer",
                        .submenu = (struct tray_menu[]) {
                                { .name = L"3D V-Cache", .pre_show = tray_default_prefer_update, .on_click = tray_default_prefer_click, .userdata=(void *)PREFER_CACHE },
                                { .name = L"Frequency", .pre_show = tray_default_prefer_update, .on_click = tray_default_prefer_click, .userdata=(void *)PREFER_FREQ },
                                { .is_end = 1 },
                        },
                },
                { .is_separator = 1 },
                { .name = L"Profiles" },
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
                break;

        case PREFER_FREQ:
                g_tray.icon.id = IDI_APP_ICON_RED;
                break;

        default:
                g_tray.icon.id = IDI_APP_ICON;
                break;
        }
}

int vcache_tray_init(HINSTANCE ins)
{
        int err;

        tray_icon_update();
        err = tray_init(&g_tray, ins);

        return err;
}

void vcache_tray_exit(void)
{
        tray_exit(&g_tray);
}
