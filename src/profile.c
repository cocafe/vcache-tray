#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>

#ifdef WIN32_LEAN_AND_MEAN
#error WIN32_LEAN_AND_MEAN defined which removes open file dialog code
#endif

#include <windows.h>
#include <commdlg.h>
#include <processthreadsapi.h>
#include <tlhelp32.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_GDI_IMPLEMENTATION
#define NK_BUTTON_TRIGGER_ON_RELEASE // to fix touch input

#include <nuklear/nuklear.h>
#include <nuklear/nuklear_gdi.h>
#include <nuklear/nuklear_jj.h>

// #define NKGDI_UPDATE_FOREGROUND_ONLY
#define NKGDI_IMPLEMENT_WINDOW
#include <nuklear/nuklear_gdiwnd.h>

#include <libjj/list.h>
#include <libjj/tray.h>
#include <libjj/iconv.h>
#include <libjj/logging.h>

#include "../asset/resource.h"
#include "profile.h"
#include "registry.h"
#include "vcache-tray.h"

extern int usrcfg_save(void);
extern void tray_option_item_update(struct tray_menu *m);
extern void tray_option_item_click(struct tray_menu *m);

static pthread_t profile_wnd_tid = 0;
static int widget_h = 40;
uint32_t nk_theme = THEME_SOLARIZED_LIGHT;

struct tray_menu nk_theme_menus[] = {
        { .name = L"Black",           .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_BLACK },
        { .name = L"White",           .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_WHITE },
        { .name = L"Red",             .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_RED },
        { .name = L"Blue",            .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_BLUE },
        { .name = L"Green",           .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_GREEN },
        { .name = L"Purple",          .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_PURPLE },
        { .name = L"Brown",           .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_BROWN },
        { .name = L"Dracula",         .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_DRACULA },
        { .name = L"Dark",            .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_DARK },
        { .name = L"Gruvbox",         .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_GRUVBOX },
        { .name = L"Solarized Light", .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_SOLARIZED_LIGHT },
        { .name = L"Solarized Dark",  .pre_show = tray_option_item_update, .on_click = tray_option_item_click, .userdata = &nk_theme, .userdata2 = (void *)THEME_SOLARIZED_DARK },
        { .is_end = 1 },
};

struct proc_sel_info {
        char name[256];
        int pid;
};

struct profile_wnd_data {
        int proc_sel_popup;
        int *proc_list_sel;
        struct proc_sel_info *proc_list_info;
        size_t proc_list_cnt;
};

int path_filename_extract(wchar_t *image_path, wchar_t *exe_name, size_t exelen)
{
        wchar_t file_name[_MAX_FNAME] = { 0 };
        wchar_t ext_name[_MAX_EXT] = { 0 };

        // WINDOWS SUCKS :)
        _wsplitpath(image_path, NULL, NULL, file_name, ext_name);

        // XXX: hardcoded _MAX_FNAME length
        swprintf(exe_name, exelen, L"%ls%ls", file_name, ext_name);

        return 0;
}

HANDLE process_snapshot_create(void)
{
        HANDLE snapshot;

        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
                pr_err("CreateToolhelp32Snapshot() failed: %lu\n", GetLastError());
                return NULL;
        }

        return snapshot;
}

int process_snapshot_iterate(HANDLE snapshot, int (*cb)(PROCESSENTRY32 *, va_list arg), ...)
{
        va_list ap;
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        int err;

        if (!cb)
                return -EINVAL;

        if (!Process32First(snapshot, &pe32)) {
                return -EFAULT;
        }

        va_start(ap, cb);

        do {
                if ((err = cb(&pe32, ap)))
                        break;
        } while (Process32Next(snapshot, &pe32));

        va_end(ap);

        return err;
}

// FIXME: O(n^)
int is_profile_list_contain(struct list_head *h, profile_t *p)
{
        profile_t *t;

        list_for_each_entry(t, h, node) {
                if (is_wstr_equal(t->name, p->name))
                        return 1;
        }

        return 0;
}

profile_t *profile_new(void)
{
        profile_t *p = calloc(1, sizeof(profile_t));
        if (!p)
                return NULL;

        INIT_LIST_HEAD(&p->node);

        return p;
}

int profile_name_generate(profile_t *p)
{
        if (is_strptr_set(p->name))
                return 0;

        wchar_t *exe = wcsstr(p->process, L".exe");
        if (!exe)
                return -EINVAL;

        memset(p->name, L'\0', sizeof(p->name));
        wcsncpy(p->name, p->process, ((intptr_t)exe - (intptr_t)p->process) / sizeof(wchar_t));

        return 0;
}

int profiles_merge(struct list_head *head, struct list_head *append)
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

void profile_list_free(struct list_head *head)
{
        profile_t *p, *s;

        list_for_each_entry_safe(p, s, head, node) {
                list_del(&p->node);
                free(p);
        }
}

int profile_list_validate(struct list_head *head, profile_t **err)
{
        profile_t *p;

        list_for_each_entry(p, head, node) {
                profile_t *pp, *tail = container_of(head->prev, profile_t, node);

                if (is_strptr_not_set(p->name)) {
                        if (err) *err = p;
                        return -EINVAL;
                }

                if (p != tail) {
                        list_for_each_entry(pp, &p->node, node) {
                                if (is_wstr_equal(p->name, pp->name)) {
                                        if (err) *err = pp;
                                        return -EALREADY;
                                }
                        }
                }
        }

        return 0;
}

static void process_selection_list_free(struct profile_wnd_data *data)
{
        if (data->proc_list_info) {
                free(data->proc_list_info);
                data->proc_list_info = NULL;
        }

        if (data->proc_list_sel) {
                free(data->proc_list_sel);
                data->proc_list_sel = NULL;
        }

        data->proc_list_cnt = 0;
}

int process_list_count(PROCESSENTRY32 *pe32, va_list arg)
{
        int *cnt = va_arg(arg, int *);

        if (cnt)
                (*cnt)++;

        return 0;
}

int process_list_info_save(PROCESSENTRY32 *pe32, va_list arg)
{
        struct profile_wnd_data *data = va_arg(arg, struct profile_wnd_data *);
        int *_i = va_arg(arg, int *);
        int i = *_i;

        data->proc_list_info[i].pid = pe32->th32ProcessID;
        iconv_wc2utf8(pe32->szExeFile, WCSLEN_BYTE(pe32->szExeFile),
                      data->proc_list_info[i].name, sizeof(data->proc_list_info[i].name));
        (*_i)++;

        return 0;
}

static int process_selection_list_build(struct profile_wnd_data *data)
{
        HANDLE snapshot = process_snapshot_create();
        size_t i = 0;
        int err = 0;

        if (!snapshot) {
                pr_mb_err("failed to get process snapshot\n");
                return -EINVAL;
        }

        process_snapshot_iterate(snapshot, process_list_count, &i);

        data->proc_list_info = calloc(i, sizeof(struct proc_sel_info));
        if (!data->proc_list_info) {
                err = -ENOMEM;
                goto out;
        }

        data->proc_list_sel = calloc(i, sizeof(int));
        if (!data->proc_list_sel) {
                err = -ENOMEM;
                goto out;
        }

        data->proc_list_cnt = i;

        i = 0;

        process_snapshot_iterate(snapshot, process_list_info_save, data, &i);

out:
        CloseHandle(snapshot);

        if (err)
                process_selection_list_free(data);

        return err;
}

int process_selection_list_show(struct nk_context *ctx, struct profile_wnd_data *data)
{
        static __thread char filter_buf[128] = { 0 };
        static __thread int filter_len = 0;
        int ret = 0;

        if (!data->proc_list_info && !data->proc_list_sel) {
                process_selection_list_build(data);
                memset(filter_buf, 0x00, sizeof(filter_buf));
                filter_len = 0;
        }

        nk_layout_row_dynamic(ctx, widget_h, 1);
        nk_edit_string(ctx, NK_EDIT_FIELD, filter_buf, &filter_len, sizeof(filter_buf), nk_filter_default);

        if (filter_len >= 0 && (size_t)filter_len < sizeof(filter_buf))
                filter_buf[filter_len] = '\0';

        filter_buf[sizeof(filter_buf) - 1] = '\0';

        nk_layout_row_dynamic(ctx, 12 * widget_h, 1);

        if (nk_group_begin(ctx, "", NK_WINDOW_BORDER)) {
                nk_layout_row_begin(ctx, NK_DYNAMIC, widget_h, 2);
                nk_layout_row_push(ctx, 0.3);
                nk_label(ctx, "PID", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.7);
                nk_label(ctx, "Process", NK_TEXT_LEFT);
                nk_layout_row_end(ctx);

                for (size_t i = 0; i < data->proc_list_cnt; ++i) {
                        char *process = data->proc_list_info[i].name;
                        char pid[16] = { 0 };

                        snprintf(pid, sizeof(pid), "%d", data->proc_list_info[i].pid);

                        if (!is_strptr_set(filter_buf) || strstr(process, filter_buf) ) {
                                nk_layout_row_begin(ctx, NK_DYNAMIC, widget_h, 2);
                                nk_layout_row_push(ctx, 0.3);
                                nk_selectable_label(ctx, pid, NK_TEXT_LEFT, &data->proc_list_sel[i]);
                                nk_layout_row_push(ctx, 0.7);
                                nk_selectable_label(ctx, is_strptr_set(process) ? process : " ", NK_TEXT_LEFT, &data->proc_list_sel[i]);
                                nk_layout_row_end(ctx);
                        }
                }

                nk_group_end(ctx);
        }

        nk_layout_row_dynamic(ctx, widget_h, 2);

        if (nk_button_label(ctx, "Add")) {
                ret = 1;
                goto close;
        }

        if (nk_button_label(ctx, "Cancel")) {
                ret = 0;
                goto close;
        }

        return 0;

close:
        memset(filter_buf, '\0', sizeof(filter_buf));
        data->proc_sel_popup = 0;
        nk_popup_close(ctx);

        return ret;
}

int profile_on_draw(struct nkgdi_window *wnd, struct nk_context *ctx)
{
        struct profile_wnd_data *data = nkgdi_window_userdata_get(wnd);
        static __thread profile_t *selected = NULL, *next = NULL;

        if (g_should_exit)
                return 0;

        if (next != selected) {
                selected = next;
        }

        nk_set_style(ctx, nk_theme);

        nk_layout_row_begin(ctx, NK_DYNAMIC, 16 * widget_h, 2);

        nk_layout_row_push(ctx, 0.4);
        if (nk_group_begin(ctx, "Profile", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
                profile_t *p;

                nk_layout_row_dynamic(ctx, widget_h, 1);

                list_for_each_entry(p, &profiles, node) {
                        char name[PROFILE_NAME_LEN] = { 0 };
                        int sel = 0;

                        if (p == selected)
                                sel = 1;

                        iconv_wc2utf8(p->name, WCSLEN_BYTE(p->name), name, sizeof(name));

                        nk_selectable_label(ctx, name[0] == '\0' ? " " : name, NK_TEXT_LEFT, &sel);

                        if (sel && p != selected) {
                                next = p;
                        }
                }

                nk_group_end(ctx);
        }

        nk_layout_row_push(ctx, 0.6);
        if (nk_group_begin(ctx, "Property", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
                char name[PROFILE_NAME_LEN] = { 0 };
                char process[PROFILE_PROCESS_LEN] = { 0 };
                int name_len = 0, process_len = 0;
                int prefer[NUM_PREFER_OPTS] = { 0 };

                if (selected) {
                        iconv_wc2utf8(selected->name, WCSLEN_BYTE(selected->name), name, sizeof(name));
                        iconv_wc2utf8(selected->process, WCSLEN_BYTE(selected->process), process, sizeof(process));
                        name_len = strlen(name);
                        process_len = strlen(process);
                        prefer[PREFER_CACHE] = selected->prefer == PREFER_CACHE ? 1 : 0;
                        prefer[PREFER_FREQ] = selected->prefer == PREFER_FREQ ? 1 : 0;
                }

                nk_layout_row_begin(ctx, NK_DYNAMIC, widget_h, 2);
                nk_layout_row_push(ctx, 0.4);
                nk_label(ctx, "Profile Name", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.6);
                nk_edit_string(ctx, NK_EDIT_FIELD, name, &name_len, sizeof(name), nk_filter_default);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, widget_h, 2);
                nk_layout_row_push(ctx, 0.4);
                nk_label(ctx, "Process", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.6);
                nk_edit_string(ctx, NK_EDIT_FIELD, process, &process_len, sizeof(process), nk_filter_default);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, widget_h, 3);
                nk_layout_row_push(ctx, 0.4);
                nk_label(ctx, "Prefer", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.3);
                if (nk_option_label(ctx, "Cache", prefer[PREFER_CACHE])) {
                        prefer[PREFER_CACHE] = 1;
                        prefer[PREFER_FREQ] = 0;
                }
                nk_layout_row_push(ctx, 0.3);
                if (nk_option_label(ctx, "Freq", prefer[PREFER_FREQ])) {
                        prefer[PREFER_CACHE] = 0;
                        prefer[PREFER_FREQ] = 1;
                }
                nk_layout_row_end(ctx);

                if (selected) {
                        name[name_len] = '\0';
                        process[process_len] = '\0';
                        memset(selected->name, '\0', sizeof(selected->name));
                        memset(selected->process, '\0', sizeof(selected->process));
                        iconv_utf82wc(name, name_len, selected->name, sizeof(selected->name));
                        iconv_utf82wc(process, process_len, selected->process, sizeof(selected->process));
                        selected->prefer = prefer[PREFER_CACHE] ? PREFER_CACHE : PREFER_FREQ;
                }

                nk_group_end(ctx);
        }

        nk_layout_row_end(ctx);

        nk_layout_row_dynamic(ctx, widget_h, 4);
        if (nk_button_label(ctx, "Add New")) {
                profile_t *n = profile_new();
                wcsncpy(n->name, L"New Profile", WCBUF_LEN(n->name));
                wcsncpy(n->process, L"new.exe", WCBUF_LEN(n->process));
                list_add_tail(&n->node, &profiles);
                next = n;
        }

        if (nk_button_label(ctx, "Add From Process List")) {
                data->proc_sel_popup = 1;
        }

        if (data->proc_sel_popup) {
                if (nk_popup_begin(ctx, NK_POPUP_STATIC,
                                   "Select Processes...",
                                   NK_WINDOW_CLOSABLE,
                                   nk_rect(20, 20, 700, 16 * widget_h))) {
                        if (process_selection_list_show(ctx, data)) {
                                for (size_t i = 0; i < data->proc_list_cnt; i++) {
                                        if (data->proc_list_sel[i]) {
                                                profile_t *n = profile_new();
                                                iconv_utf82wc(data->proc_list_info[i].name,
                                                              sizeof(data->proc_list_info[i].name),
                                                              n->process,
                                                              sizeof(n->process));
                                                if (profile_name_generate(n) == -EINVAL) {
                                                        wcsncpy(n->name, n->process, WCBUF_LEN(n->name));
                                                }
                                                list_add_tail(&n->node, &profiles);
                                                next = n;
                                        }
                                }
                        }

                        nk_popup_end(ctx);
                } else {
                        data->proc_sel_popup = 0;
                }
        } else {
                process_selection_list_free(data);
        }

        if (nk_button_label(ctx, "Add From Files")) {
                OPENFILENAME ofn = { 0 };
                wchar_t filepath[PATH_MAX] = {0 };

                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = NULL;
                ofn.lpstrFile = filepath;
                ofn.nMaxFile = WCBUF_LEN(filepath);
                ofn.lpstrFilter = L"Executable File (*.exe)\0*.EXE\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = NULL;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (GetOpenFileName(&ofn) == TRUE)
                {
                        profile_t *n = profile_new();
                        path_filename_extract(filepath, n->process, WCBUF_LEN(n->process));
                        profile_name_generate(n);
                        list_add_tail(&n->node, &profiles);
                        next = n;
                }
        }

        if (nk_button_label(ctx, "Delete") && selected) {
                pthread_mutex_lock(&profiles_lock);

                list_del(&selected->node);
                free(selected);

                pthread_mutex_unlock(&profiles_lock);

                next = list_empty(&profiles) ? NULL : container_of(profiles.prev, profile_t, node);
        }

        nk_layout_row_dynamic(ctx, widget_h, 1);
        nk_label(ctx, "", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, widget_h, 5);
        nk_label(ctx, "", NK_TEXT_LEFT);
        nk_label(ctx, "", NK_TEXT_LEFT);
        nk_label(ctx, "", NK_TEXT_LEFT);
        if (nk_button_label(ctx, "Apply")) {
                profile_t *p = NULL;
                int err = profile_list_validate(&profiles, &p);

                if (err) {
                        switch (err) {
                        case -EINVAL:
                                pr_mb_err("Registry cannot have empty key name\n");
                                break;

                        case -EALREADY:
                                pr_mb_err("Registry cannot have same profile name: \"%ls\"\n", p->name);
                                break;
                        }

                        next = p;
                } else {
                        if (profiles_registry_write(&profiles) == 0)
                                mb_info("Profiles are written to registry successfully%s", restart_svc ? "\nServices restarted successfully" : "");
                        else
                                mb_err("Failed to apply");
                }
        }
        if (nk_button_label(ctx, "Save")) {
                if (usrcfg_save() == 0)
                        mb_info("Config saved to %s", json_path);
                else
                        mb_err("Failed to save to %s", json_path);
        }

        return 1;
}

void *profile_gui_worker(void *data)
{
        struct nkgdi_window nkwnd = { 0 };
        struct profile_wnd_data wnd_data = { 0 };

        nkwnd.allow_move = 1;
        nkwnd.allow_sizing = 1;
        nkwnd.allow_maximize = 0;
        nkwnd.has_titlebar = 1;
        nkwnd.font_name = "ubuntu mono mod";
        nkwnd.cb_on_draw = profile_on_draw;
        nkwnd.cb_on_close = NULL;

        nkgdi_window_create(&nkwnd, 800, (17 + 2 + 2) * widget_h, "Edit Profiles", 0, 0);
        nkgdi_window_icon_set(&nkwnd, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON)));
        nkgdi_window_userdata_set(&nkwnd, &wnd_data);
        nkgdi_window_set_center(&nkwnd);
        nk_set_style(nkgdi_window_nkctx_get(&nkwnd), nk_theme);

        nkgdi_window_blocking_update(&nkwnd);

        nkgdi_window_destroy(&nkwnd);

        WRITE_ONCE(profile_wnd_tid, 0);

        pthread_exit(NULL);

        return NULL;
}

int profile_gui_show(void)
{
        pthread_t tid = 0;
        int err;

        if (READ_ONCE(profile_wnd_tid) != 0) {
                return -EALREADY;
        }

        WRITE_ONCE(profile_wnd_tid, -1);

        if ((err = pthread_create(&tid, NULL, profile_gui_worker, NULL))) {
                pr_mb_err("failed to create GUI thread\n");
                WRITE_ONCE(profile_wnd_tid, 0);

                return -err;
        }

        WRITE_ONCE(profile_wnd_tid, tid);

        return 0;
}

void profile_gui_init(void)
{
        nkgdi_window_init();
}

void profile_gui_deinit(void)
{
        if (READ_ONCE(profile_wnd_tid))
                pthread_join(profile_wnd_tid, NULL);

        nkgdi_window_shutdown();
}