/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>  
#include <regex.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "CA.h"
#include "recovery_ui.h"
#include "socket_server.h"
#include "utils.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "cloudstorage.h"

//#define ENABLE_YUHUA_LCD_TEST (0x1)

static const struct option OPTIONS[] = {
    { "send_intent", required_argument, NULL, 's' },
    { "update_package", required_argument, NULL, 'u' },
    { "wipe_data", no_argument, NULL, 'w' },
    { "wipe_cache", no_argument, NULL, 'c' },
    { "socket_server", no_argument, NULL, 'v' },
    { NULL, 0, NULL, 0 },
};

static const char *COMMAND_FILE = "CACHE:recovery/command";
static const char *INTENT_FILE = "CACHE:recovery/intent";
static const char *LOG_FILE = "CACHE:recovery/log";
static const char *SD_LOG_FILE = "SDCARD:update/update.log";
static const char *SDCARD_PACKAGE_FILE = "SDCARD:update.zip";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=root:path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_root() reformats /data
 * 6. erase_root() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=CACHE:some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_root() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 *
 * ENCRYPTED FILE SYSTEMS ENABLE/DISABLE
 * 1. user selects "enable encrypted file systems"
 * 2. main system writes "--set_encrypted_filesystem=on|off" to
 *    /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and
 *    "--set_encrypted_filesystems=on|off"
 *    -- after this, rebooting will restart the transition --
 * 5. read_encrypted_fs_info() retrieves encrypted file systems settings from /data
 *    Settings include: property to specify the Encrypted FS istatus and
 *    FS encryption key if enabled (not yet implemented)
 * 6. erase_root() reformats /data
 * 7. erase_root() reformats /cache
 * 8. restore_encrypted_fs_info() writes required encrypted file systems settings to /data
    *    Settings include: property to specify the Encrypted FS status and
*    FS encryption key if enabled (not yet implemented)
    * 9. finish_recovery() erases BCB
    *    -- after this, rebooting will restart the main system --
    * 10. main() calls reboot() to boot main system
    */

    static const int MAX_ARG_LENGTH = 4096;
    static const int MAX_ARGS = 100;

    // open a file given in root:path format, mounting partitions as necessary
    static FILE*
    fopen_root_path(const char *root_path, const char *mode) {
        if (ensure_root_path_mounted(root_path) != 0) {
            LOGE("Can't mount %s\n", root_path);
            return NULL;
        }

        char path[PATH_MAX] = "";
        if (translate_root_path(root_path, path, sizeof(path)) == NULL) {
            LOGE("Bad path %s\n", root_path);
            return NULL;
        }

        // When writing, try to create the containing directory, if necessary.
        // Use generous permissions, the system (init.rc) will reset them.
        if (strchr("wa", mode[0])) dirCreateHierarchy(path, 0777, NULL, 1);

        FILE *fp = fopen(path, mode);
        return fp;
    }

// close a file, log an error if the error indicator is set
static void
check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen_root_path(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                (*argv)[*argc] = strdup(strtok(buf, "\r\n"));  // Strip newline.
            }

            check_and_fclose(fp, COMMAND_FILE);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    }

    // --> write the arguments we have back into the bootloader control block
    // always boot into recovery after this (until finish_recovery() is called)
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    int i;
    for (i = 1; i < *argc; ++i) {
        strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
        strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
    set_bootloader_message(&boot);
}

static void
set_sdcard_update_bootloader_message() {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
static void
finish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_root_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

#if 1
    // Copy logs to sdcard so the system can find out what happened.	
    FILE *log = fopen_root_path(SD_LOG_FILE, "r+");
    if (log==NULL)
        log = fopen_root_path(SD_LOG_FILE, "a");
    if (log==NULL)
        log = fopen_root_path(LOG_FILE, "r+");
    if (log==NULL)
        log = fopen_root_path(LOG_FILE, "a");

    if (log != NULL) {
        FILE *tmplog = fopen(TEMPORARY_LOG_FILE, "r");
        if (tmplog == NULL) {
            LOGE("Can't open %s\n", TEMPORARY_LOG_FILE);
        } else {
            static long tmplog_offset = 0;
            fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            tmplog_offset = ftell(tmplog);
            check_and_fclose(tmplog, TEMPORARY_LOG_FILE);
        }
        check_and_fclose(log, SD_LOG_FILE);
    }
#else
    // Copy logs to cache so the system can find out what happened.
    FILE *log = fopen_root_path(LOG_FILE, "a");
    if (log == NULL) {
        LOGE("Can't open %s\n", LOG_FILE);
    } else {
        FILE *tmplog = fopen(TEMPORARY_LOG_FILE, "r");
        if (tmplog == NULL) {
            LOGE("Can't open %s\n", TEMPORARY_LOG_FILE);
        } else {
            static long tmplog_offset = 0;
            fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            tmplog_offset = ftell(tmplog);
            check_and_fclose(tmplog, TEMPORARY_LOG_FILE);
        }
        check_and_fclose(log, LOG_FILE);
    }
#endif

    // Reset to mormal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    char path[PATH_MAX] = "";
    if (ensure_root_path_mounted(COMMAND_FILE) != 0 ||
            translate_root_path(COMMAND_FILE, path, sizeof(path)) == NULL ||
            (unlink(path) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

    sync();  // For good measure.
}

static int
erase_root(const char *root) {
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    ui_print("Formatting %s...\n", root);
    return format_root_device(root);
}

char**
prepend_title(char** headers) {
    char* title[] = { "MetaOS system utility 1.0"
        "",
            "",
            NULL };

    // count the number of lines in our title, plus the
    // caller-provided headers.
    int count = 0;
    char** p;
    for (p = title; *p; ++p, ++count);
    for (p = headers; *p; ++p, ++count);

    char** new_headers = malloc((count+1) * sizeof(char*));
    char** h = new_headers;
    for (p = title; *p; ++p, ++h) *h = *p;
    for (p = headers; *p; ++p, ++h) *h = *p;
    *h = NULL;

    return new_headers;
}

int
get_menu_selection(char** headers, char** items, int menu_only) {
    // throw away keys pressed previously, so user doesn't
    // accidentally trigger menu items.
    ui_clear_key_queue();

    ui_start_menu(headers, items);
    int selected = 0;
    int chosen_item = -1;

    while (chosen_item < 0) {
        int key = ui_wait_key();
        int visible = ui_text_visible();

        int action = device_handle_key(key, visible);

        if (action < 0) {
            if (MENU_BACK==action) {
                chosen_item = action;  /* exit menu */
                break;
            }

            switch (action) {
                case HIGHLIGHT_UP:
                    --selected;
                    selected = ui_menu_select(selected);
                    break;
                case HIGHLIGHT_DOWN:
                    ++selected;
                    selected = ui_menu_select(selected);
                    break;
                case SELECT_ITEM:
                    chosen_item = selected;
                    break;
                case NO_ACTION:
                    break;
            }
        } else if (!menu_only) {
            chosen_item = action;
        }
    }

    ui_end_menu();
    return chosen_item;
}

static void
wipe_data(int confirm) {
    if (confirm) {
        static char** title_headers = NULL;

        if (title_headers == NULL) {
            char* headers[] = { "Confirm wipe of all user data?",
                "  THIS CAN NOT BE UNDONE.",
                "",
                NULL };
            title_headers = prepend_title(headers);
        }

        char* items[] = { " No",
            " No",
            " No",
            " No",
            " No",
            " No",
            " No",
            " Yes -- delete all user data",   // [7]
            " No",
            " No",
            " No",
            NULL };

        int chosen_item = get_menu_selection(title_headers, items, 1);
        if (chosen_item != 7) {
            return;
        }
    }

    ui_print("\n-- Wiping data...\n");
    device_wipe_data();
    erase_root("DATA:");
    erase_root("CACHE:");
    ui_print("Data wipe complete.\n");
}


static int is_connect_retry_needed() {
    char retry[256];
    property_get("init.connect.retry",retry,"0");
    if(!strcmp(retry,"0")) {
        return 0;
    }
    return 1;
}

static void enter_remote_update();

static void choose_connection_method() {
    static char** title_headers = NULL;

    if(title_headers == NULL) {
        char* headers[] = { "Choose your connection method below",
            NULL};
        title_headers = prepend_title(headers);
    }
    char* items[] = { "TD",
        "Wifi",
        NULL};
    int chosen_item = get_menu_selection(title_headers,items,1);
    pid_t pc = fork();
    if(pc < 0) {
        ui_print("fork failed");
    }else if(pc==0) {
        switch(chosen_item) {
            case 0:
                if(execl("/system/bin/sh","sh","/system/etc/connect_td.sh",NULL) < 0) {
                    ui_print("connect td failed");
                }
                break;
            case 1:
                if(execl("/system/bin/sh","sh","/system/etc/connect_wifi.sh",NULL) < 0) {
                    ui_print("connect wifi failed");
                }
                break;
            default:
                break;

        }
    }else {
        wait(NULL);
        enter_remote_update();
    }

}


static void update_preference(char* uid, char* signature) {
    system("mount -t yaffs2 /dev/block/mtdblock12 /mnt/data");
    //const MtdPartition* data = mtd_find_partition_by_name("data");
    //mkdir("/mnt/data",0755);
    //mtd_mount_partition(data,"/mnt/data","yaffs2",0);
    CURLcode res = downloadCloudFile("/mnt/data/data/com.android.settings/files/wallpaper",uid,signature,"wallpaper");
    if( CURLE_OK == res) {
        ui_print("download wallpaper success!\n");
    }
    system("umount /mnt/data");
    //umount("/mnt/data");
}


extern char name[256][256];
extern char scripts[256][256];

static void enter_remote_update() {
    char imsi[256],filename[64],uid[64];
    memset(imsi, 0, sizeof(imsi));
    memset(filename, 0, sizeof(filename));
    memset(uid, 0, sizeof(uid));
    if(!is_imsi_got(imsi)) {
        ui_print("IMSI not got, please check your sim card");
        goto END;
    }

    if(!is_priKey_exist(imsi,filename,uid)) {
        ui_print("priKey not exist\n");
        char pri[1024];
        if(check_register(imsi,pri,uid)) {
            ui_print("check register sucess\n");
            char xrandom[64];
            snprintf(filename,sizeof(filename),"/sdcard/%s_%s_cpk.pem",imsi,uid);
            save_priKey(pri,filename);
GET_PRI:
            ui_print("uid:%s\n",uid);
            char command[512], ret[256], signature[256];
            memset(command, 0, sizeof(command));
            memset(ret, 0, sizeof(ret));
            memset(signature, 0, sizeof(signature));
            memset(xrandom, 0, sizeof(xrandom));
            snprintf(command, sizeof(command), "/system/bin/cpk -set-identity %s",uid);
            system(command);
            memset(command,0,sizeof(command));
            snprintf(command, sizeof(command), "/system/bin/cpk -import-sign-key -in %s -pass %s%s", filename, imsi ,"0123456789abcdefghijklmnopqrstuv");
            system(command);
            ui_print("import sign key success\n");
            if(apply_random(uid,xrandom)) {
                ui_print("applied random number %s\n",xrandom);
                memset(command, 0, sizeof(command));
                snprintf(command, sizeof(command), "echo %s | head -c %d | /system/bin/cpk -sign -pass %s%s > /sdcard/.signature", xrandom, strlen(xrandom), imsi, "0123456789abcdefghijklmnopqrstuv");
                system(command);
                FILE* stream = fopen("/sdcard/.signature","r");
                int size = 0;
                if(stream != NULL) {
                    size = fread( ret, sizeof(char), sizeof(ret), stream);
                }
                ret[size] = 0;
                ui_print("signature raw string is %s\n",ret);
                fclose(stream);
                if(size <= 0) {
                    ui_print("read sign result failed\n");
                    goto END;
                }		
                size = URLEncode(ret,size,signature,sizeof(signature));
                signature[size - 3] = 0;
                ui_print("encoded signature is %s\n",signature);
                if(cpk_authentication(uid,signature) == CA_TRUE) {
                    ui_print("authentication success\n");
                    update_preference(uid,signature);
                }
            }else {
                ui_print("apply random failed\n");
            }
        }else {
            ui_print("check register failed\n");
        }
    }else {
        goto GET_PRI;
    }

    static char** title_headers = NULL;

    if(title_headers == NULL) {
        char* headers[] = { "Update to the version below?",
            "  THIS CAN NOT BE UNDONE.",
            "",
            NULL};
        title_headers =  prepend_title(headers);
    }

    int num  = find_sdcard_update_script();

    char* items[] = { "MetaOS 1.0",
        "MetaOS 1.1",
        NULL};
    int chosen_item;
    if(num > 0) {
        chosen_item = get_menu_selection(title_headers,name,0);
    }else {
        chosen_item = get_menu_selection(title_headers,items,0);
    }
    pid_t pc = fork();
    if(pc < 0) {
        ui_print("fork failed\n");
    } else if(pc == 0) {
        if(num > 0) {
            if(execl("/system/bin/sh", "sh", scripts[chosen_item],NULL) < 0) {
                ui_print("execute failed\n");
            }
        }else {
            switch(chosen_item) {
                case 0:
                    if(execl("/system/bin/sh","sh","/system/etc/tnosupdate.sh",NULL) < 0) {
                        ui_print("execute failed\n");
                    }
                    break;
                case 1:
                    if(execl("/system/bin/sh","sh","/system/etc/tnosupdate-1.sh",NULL) < 0) {
                        ui_print("execute failed\n");
                    }
                    break;
                default:
                    break;
            };
        }
    }else {
        wait(NULL);
    }

END:
    return;
}
// yuhua special here
static char* YH_MENU_HEADERS[] = { NULL };
static char* YH_MENU_ITEMS[] = { "Reboot system now",
    "Apply /sdcard/update/*.zip",
    "Factory Reset/Wipe Data",
    "Board Test",
    NULL };

static void yh_recovery_promote()
{
    char system_ver[PROPERTY_VALUE_MAX] = {0};

#if defined(CONFIG_BOARD_LANDMARK)
    open_modem_download();
#endif

    property_get("ro.product.model", system_ver, "unknow");    
    ui_print("RecVer:%s", system_ver);
    property_get("ro.build.id", system_ver, "unknow");	  
    ui_print(" %s", system_ver);
    property_get("ro.build.version.incremental", system_ver, "unknow");   
    ui_print(" %s\n", system_ver);

    ui_print("HOME+VolumeUp/VolumeDown to select a menu\n");
}

static char** sdcard_update_title_headers = NULL;
static void yh_sdcard_update_build_titles()
{
    if (sdcard_update_title_headers == NULL) {
        char* headers[] = { "Select a update package:",
            "",
            NULL };
        sdcard_update_title_headers = prepend_title(headers);
    }
}

static char* g_yuhua_sd_package_items[YUHUA_UPDATE_MAX_MENU_ITEMS+1];
#include <dirent.h>
static int yh_sdcard_update_build_items()
{
    int pack_num = 0;
    struct dirent* entry;
    char* updateZip = NULL;
    DIR* dir = opendir(YUHUA_UPDATE_DIR);

    if (dir == NULL) {
        LOGE("Cannot open %s\n", YUHUA_UPDATE_DIR);
        goto out;
    }

    while (1) { 
        entry = readdir(dir);
        if (entry == NULL || pack_num>=YUHUA_UPDATE_MAX_MENU_ITEMS)
            break;

        if (strstr(entry->d_name, ".zip")) {
            if (g_yuhua_sd_package_items[pack_num])
                free(g_yuhua_sd_package_items[pack_num]);
            g_yuhua_sd_package_items[pack_num] = malloc(strlen(entry->d_name)+1);
            strcpy(g_yuhua_sd_package_items[pack_num], entry->d_name);
            pack_num++;
            LOGI("Find package %s in %s \n", entry->d_name, YUHUA_UPDATE_DIR);
        }
    }

out:	
    if (dir)
        closedir(dir);
    g_yuhua_sd_package_items[pack_num] = NULL;
    return pack_num;
}
static int yh_sdcard_update()
{
    int pack_num;
    int ret = INSTALL_ERROR;	

    /* mount sdcard */
    ensure_root_path_mounted(SDCARD_PACKAGE_FILE); 

    yh_sdcard_update_build_titles();

    pack_num = yh_sdcard_update_build_items();
    if (pack_num>0) {
        int chosen_item = get_menu_selection(sdcard_update_title_headers, g_yuhua_sd_package_items, 1);
        if (chosen_item>=0) {
            char sdcard_pack_file[256];

            char** title_headers;
            char* headers[4] ;
            headers[0] = "Update Package Managment";
            headers[1] = g_yuhua_sd_package_items[chosen_item];
            headers[2] = "";
            headers[3] = NULL;
            title_headers = prepend_title(headers);
            char* items[] = { " No",
                " No",
                " No",
                " UPDATE from this package", // [3]
                " No",
                " No",
                " No",
                " DELETE this package",   // [7]
                " No",
                " No",
                " No",
                NULL };			
            int action_item = get_menu_selection(title_headers, items, 1);
            if (7==action_item) {
                sdcard_pack_file[0] = 0x0;			
                strcat(sdcard_pack_file, YUHUA_UPDATE_DIR);
                strcat(sdcard_pack_file, g_yuhua_sd_package_items[chosen_item]);	
                unlink(sdcard_pack_file);
                ui_print("Delete %s succ\n", sdcard_pack_file);				
            } else if (3==action_item) {
                sdcard_pack_file[0] = 0x0;			
                strcat(sdcard_pack_file, "SDCARD:update/");
                strcat(sdcard_pack_file, g_yuhua_sd_package_items[chosen_item]);			 
                ui_print("\nInstalling %s\n", sdcard_pack_file);
                ret = install_package(sdcard_pack_file);
            } else {
                ui_print("User canceled update process\n");
            }
        }
    }else {
        ui_print("Can't find package in SDCARD\n");
    }

    return ret;
}

static void
prompt_and_wait() {
    char** headers = prepend_title(MENU_HEADERS);

    yh_recovery_promote();

    for (;;) {
        finish_recovery(NULL);
        ui_reset_progress();

        int chosen_item = get_menu_selection(headers, MENU_ITEMS, 0);

        // device-specific code may take some action here.  It may
        // return one of the core actions handled in the switch
        // statement below.
        chosen_item = device_perform_action(chosen_item);

        switch (chosen_item) {
            case ITEM_REBOOT:
                return;

            case ITEM_WIPE_DATA:
                wipe_data(ui_text_visible());
                if (!ui_text_visible()) return;
                break;

            case ITEM_WIPE_CACHE:
                ui_print("\n-- Wiping cache...\n");
                erase_root("CACHE:");
                ui_print("Cache wipe complete.\n");
                if (!ui_text_visible()) return;
                break;

            case ITEM_APPLY_SDCARD:
                //ui_print("\n-- Install from sdcard...\n");
                set_sdcard_update_bootloader_message();
                //int status = install_package(SDCARD_PACKAGE_FILE);
                int status = yh_sdcard_update(); // use our own sdcard update routine, by frank
                if (status != INSTALL_SUCCESS) {
                    ui_set_background(BACKGROUND_ICON_ERROR);
                    ui_print("Installation aborted.\n");
                } else if (!ui_text_visible()) {
                    return;  // reboot if logs aren't visible
                } else {
                    ui_print("\nInstall from sdcard complete.\n");
                }
                break;

            case ITEM_UPDATE:
                ui_print("\n-- Running update script...\n");
                pid_t pc = fork();
                if(pc < 0) {
                    ui_print("fork failed");
                } else if( pc == 0) {
                    if(execl("/system/bin/sh","sh","/system/etc/init.connect",NULL) <0) {
                        ui_print("execute failed");
                    }
                } else {
                    wait(NULL);
                }
                choose_connection_method();
                break;
        }
    }
}

static void
print_property(const char *key, const char *name, void *cookie) {
    fprintf(stderr, "%s=%s\n", key, name);
}


int
main(int argc, char **argv) {
    time_t start = time(NULL);

    // If these fail, there's not really anywhere to complain...
    freopen(TEMPORARY_LOG_FILE, "a", stdout); setbuf(stdout, NULL);
    freopen(TEMPORARY_LOG_FILE, "a", stderr); setbuf(stderr, NULL);
    fprintf(stderr, "Starting recovery on %s", ctime(&start));

    ui_init();
#ifdef ENABLE_YUHUA_LCD_TEST
    lcd_test(1);
#endif
    get_args(&argc, &argv);

    initCurl();
    system("/system/bin/cpk -import-param -in /system/etc/cpk/public_params.der");
    //cpktool_import_params("/system/etc/cpk/public_params.der");

    int previous_runs = 0;
    const char *send_intent = NULL;
    const char *update_package = NULL;
    int wipe_data = 0, wipe_cache = 0, socket_server = 0;

    int arg;
    while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) {
        switch (arg) {
            case 'p': previous_runs = atoi(optarg); break;
            case 's': send_intent = optarg; break;
            case 'u': update_package = optarg; break;
            case 'w': wipe_data = wipe_cache = 1; break;
            case 'c': wipe_cache = 1; break;
            case 'v': socket_server = 1; break;
            case '?':
                      LOGE("Invalid command argument\n");
                      continue;
        }
    }

    device_recovery_start();

    fprintf(stderr, "Command:");
    for (arg = 0; arg < argc; arg++) {
        fprintf(stderr, " \"%s\"", argv[arg]);
    }
    fprintf(stderr, "\n\n");

    property_list(print_property, NULL);
    fprintf(stderr, "\n");

    int status = INSTALL_SUCCESS;

    if(socket_server) {
        ui_print("Enter socket server");
        ui_set_background(BACKGROUND_ICON_INSTALLING);
        ui_show_progress(0.0f,VERIFICATION_PROGRESS_TIME);
        //erase_root("CACHE:");
        finish_recovery(send_intent);
        status = init_server();
        prompt_and_wait();

    }else if (update_package != NULL) {
        status = install_package(update_package);
        if (status != INSTALL_SUCCESS) ui_print("Installation aborted.\n");
    } else if (wipe_data) {
        if (device_wipe_data()) status = INSTALL_ERROR;
        if (erase_root("DATA:")) status = INSTALL_ERROR;
        if (wipe_cache && erase_root("CACHE:")) status = INSTALL_ERROR;
        if (status != INSTALL_SUCCESS) ui_print("Data wipe failed.\n");
    } else if (wipe_cache) {
        if (wipe_cache && erase_root("CACHE:")) status = INSTALL_ERROR;
        if (status != INSTALL_SUCCESS) ui_print("Cache wipe failed.\n");
    } else {
        status = INSTALL_ERROR;  // No command specified
    }

    if (status != INSTALL_SUCCESS) ui_set_background(BACKGROUND_ICON_ERROR);
    //if (status != INSTALL_SUCCESS || ui_text_visible()) prompt_and_wait();
    if (status != INSTALL_SUCCESS) prompt_and_wait(); // rm ui_text_visible() by frank for yuhua updater

    // Otherwise, get ready to boot the main system...
    finish_recovery(send_intent);
    ui_print("Rebooting...\n");
    cleanupCurl();
    sync();
    reboot(RB_AUTOBOOT);
    return EXIT_SUCCESS;
}
