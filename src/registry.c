#include <stdint.h>

#include <windows.h>

#include <libjj/list.h>
#include <libjj/utils.h>
#include <libjj/logging.h>

#include "registry.h"
#include "profile.h"
#include "vcache-tray.h"

int amd3dv_service_restart(void)
{
        int err;

        if ((err = system("powershell -command \"Restart-Service amd3dvcacheSvc -Force\"")))
                pr_err("failed to restart amd3dvacheSvc\n");

        return err;
}

int default_prefer_registry_read(uint32_t *ret)
{
        HKEY key_prefer;
        DWORD data, sz = sizeof(DWORD);
        int err;

        err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_KEY_PREFERENCES, 0, KEY_READ, &key_prefer);
        if (err != ERROR_SUCCESS) {
                pr_err("failed to open %ls, err = 0x%x\n", REG_KEY_PREFERENCES, err);
                return err;
        }

        err = RegGetValue(key_prefer, NULL, REG_VAL_DEFAULT_TYPE, RRF_RT_REG_DWORD, NULL, &data, &sz);
        if (err != ERROR_SUCCESS) {
                pr_err("failed to read %ls\\%ls, err = 0x%x\n", REG_KEY_PREFERENCES, REG_VAL_DEFAULT_TYPE, err);
                goto out;
        }

        if (ret)
                *ret = (uint32_t)data;

out:
        RegCloseKey(key_prefer);

        return 0;
}

int __default_prefer_registry_write(uint32_t val)
{
        HKEY key_prefer;
        int err;

        err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_KEY_PREFERENCES, 0, KEY_WRITE, &key_prefer);
        if (err != ERROR_SUCCESS) {
                pr_err("failed to read %ls, err = 0x%x\n", REG_KEY_PREFERENCES, err);
                return err;
        }

        err = RegSetValueEx(key_prefer, REG_VAL_DEFAULT_TYPE, 0, REG_DWORD, (LPBYTE)(&(DWORD){val }), sizeof(DWORD));
        if (err != ERROR_SUCCESS) {
                pr_err("failed to write %ls\\%ls, err = 0x%x\n", REG_KEY_PREFERENCES, REG_VAL_DEFAULT_TYPE, err);
        }

        RegCloseKey(key_prefer);

        return 0;
}

int default_prefer_registry_write(uint32_t val)
{
        int err = __default_prefer_registry_write(val);
        if (err == ERROR_SUCCESS && restart_svc) {
                amd3dv_service_restart();
        }

        return err;
}

int profiles_registry_read(struct list_head *head)
{
        WCHAR subkey_name[255] = { 0 };
        HKEY key_app;
        int err;

        err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_KEY_APP, 0, KEY_ALL_ACCESS, &key_app);
        if (err != ERROR_SUCCESS) {
                pr_err("failed to open %ls, err = 0x%x\n", REG_KEY_APP, err);
                return err;
        }

        for (DWORD i = 0; ERROR_SUCCESS == (err = RegEnumKey(key_app, i++, subkey_name, ARRAY_SIZE(subkey_name))); ) {
                wchar_t process[255] = { 0 };
                DWORD type, sz;
                wchar_t sub_path[512] = { 0 };
                HKEY sub_key;
                profile_t *p;

                snwprintf(sub_path, ARRAY_SIZE(sub_path), L"%ls\\%ls", REG_KEY_APP, subkey_name);

                err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, sub_path, 0, KEY_ALL_ACCESS, &sub_key);
                if (err != ERROR_SUCCESS) {
                        pr_err("failed to open %ls, err = 0x%x\n", sub_path, err);
                        continue;
                }

                sz = sizeof(process);
                err = RegGetValue(sub_key, NULL, REG_VAL_ENDSWITH, RRF_RT_REG_SZ, NULL, process, &sz);
                if (err != ERROR_SUCCESS) {
                        pr_err("failed to read %ls\\%ls, err = 0x%x\n", REG_KEY_APP, REG_VAL_ENDSWITH, err);
                        goto subkey_close;
                }

                sz = sizeof(DWORD);
                err = RegGetValue(sub_key, NULL, REG_VAL_PREFER_TYPE, RRF_RT_REG_DWORD, NULL, &type, &sz);
                if (err != ERROR_SUCCESS) {
                        pr_err("failed to read %ls\\%ls, err = 0x%x\n", REG_KEY_APP, REG_VAL_ENDSWITH, err);
                        goto subkey_close;
                }

                p = calloc(1, sizeof(profile_t));
                if (!p) {
                        pr_err("failed to allocate memory\n");
                        goto subkey_close;
                }

                INIT_LIST_HEAD(&p->node);
                wcsncpy(p->name, subkey_name, ARRAY_SIZE(p->name));
                wcsncpy(p->process, process, ARRAY_SIZE(p->process));
                p->prefer = !!type;

                list_add_tail(&p->node, head);

subkey_close:
                RegCloseKey(sub_key);
        }

        RegCloseKey(key_app);

        return 0;
}

int profiles_registry_clean(void)
{
        HKEY key_app;
        int err;

        err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_KEY_APP, 0, KEY_ALL_ACCESS, &key_app);
        if (err != ERROR_SUCCESS) {
                pr_err("failed to open %ls, err = 0x%x\n", REG_KEY_APP, err);
                return err;
        }

        err = RegDeleteTree(key_app, NULL);
        if (err != ERROR_SUCCESS) {
                pr_err("failed to delete %ls, err = 0x%x\n", REG_KEY_APP, err);
                goto close;
        }

        RegCloseKey(key_app);

        err = RegCreateKey(HKEY_LOCAL_MACHINE, REG_KEY_APP, &key_app);
        if (err != ERROR_SUCCESS) {
                pr_err("failed to create %ls, err = 0x%x\n", REG_KEY_APP, err);
                goto out;
        }

close:
        RegCloseKey(key_app);

out:
        return err;
}

int __profiles_registry_write(struct list_head *head)
{
        profile_t *p;
        HKEY key_app;
        int err;

        if ((err = profiles_registry_clean()))
                return err;

        err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_KEY_APP, 0, KEY_ALL_ACCESS, &key_app);
        if (err != ERROR_SUCCESS) {
                pr_err("failed to open %ls, err = 0x%x\n", REG_KEY_APP, err);
                return err;
        }

        list_for_each_entry(p, head, node) {
                HKEY sub_key;

                if (is_strptr_not_set(p->name)) {
                        pr_warn("name of profile associates process \"%ls\" is invalid\n", p->process);
                        continue;
                }

                err = RegCreateKey(key_app, p->name, &sub_key);
                if (err != ERROR_SUCCESS) {
                        pr_err("failed to create %ls\\%ls, err = 0x%x\n", REG_KEY_APP, p->name, err);
                        continue;
                }

                err = RegSetValueEx(sub_key, REG_VAL_ENDSWITH, 0, REG_SZ, (void *)p->process, WCSLEN_BYTE(p->process));
                if (err != ERROR_SUCCESS && err != ERROR_MORE_DATA) {
                        pr_err("failed to create %ls\\%ls\\%ls, err = 0x%x\n", REG_KEY_APP, p->name, REG_VAL_ENDSWITH, err);
                        goto subkey_close;
                }

                err = RegSetValueEx(sub_key, REG_VAL_PREFER_TYPE, 0, REG_DWORD, (void *)&p->prefer, sizeof(p->prefer));
                if (err != ERROR_SUCCESS) {
                        pr_err("failed to create %ls\\%ls\\%ls, err = 0x%x\n", REG_KEY_APP, p->name, REG_VAL_PREFER_TYPE, err);
                        goto subkey_close;
                }

                pr_info("\"%ls\\%ls\" with process: \"%ls\" type: %u\n", REG_KEY_APP, p->name, p->process, p->prefer);

subkey_close:
                RegCloseKey(sub_key);
        }

        RegCloseKey(key_app);

        return err;
}

int profiles_registry_write(struct list_head *head)
{
        int err = __profiles_registry_write(head);
        if (err == ERROR_SUCCESS && restart_svc) {
                err = amd3dv_service_restart();
        }

        return err;
}
