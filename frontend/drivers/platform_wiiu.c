/* RetroArch - A frontend for libretro.
 *  Copyright (C) 2014-2016 - Ali Bouhlel
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 * RetroArch is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Found-
 * ation, either version 3 of the License, or (at your option) any later version.
 *
 * RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with RetroArch.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/iosupport.h>
#include <net/net_compat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <wiiu/types.h>
#include <wiiu/ac.h>
#include <file/file_path.h>

#ifndef IS_SALAMANDER
#include <lists/file_list.h>
#endif

#include <string/stdstring.h>

#include <coreinit/dynload.h>
#include <coreinit/ios.h>
#include <coreinit/foreground.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <proc_ui/procui.h>
#include <padscore/wpad.h>
#include <padscore/kpad.h>
#include <sysapp/launch.h>
#include <gx2/event.h>

#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/log_cafe.h>

#include "../frontend.h"
#include "../frontend_driver.h"
#include "../../file_path_special.h"
#include "../../defaults.h"
#include "../../paths.h"
#include "../../retroarch.h"
#include "../../verbosity.h"
#include "../../tasks/tasks_internal.h"

#ifndef IS_SALAMANDER
#ifdef HAVE_MENU
#include "../../menu/menu_driver.h"
#endif

#ifdef HAVE_NETWORKING
#include "../../network/netplay/netplay.h"
#endif
#endif

#ifdef HAVE_LIBMOCHA
#include <mocha/mocha.h>
#ifdef HAVE_LIBFAT
#include <fat.h>
#endif
#endif

#include "wiiu_dbg.h"
#include "system/exception_handler.h"
#include "system/memory.h"

#define WIIU_SD_PATH "fs:/vol/external01/"
#define WIIU_SD_FAT_PATH "sd:/"
#define WIIU_USB_FAT_PATH "usb:/"

/**
 * The Wii U frontend driver, along with the main() method.
 */

#ifndef IS_SALAMANDER
static enum frontend_fork wiiu_fork_mode = FRONTEND_FORK_NONE;
static bool have_libfat_usb = false;
static bool have_libfat_sdcard = false;
#endif
static bool in_exec = false;

static bool exists(char *path)
{
   struct stat stat_buf = {0};

   if (!path)
      return false;

   return (stat(path, &stat_buf) == 0);
}

static void fix_asset_directory(void)
{
   char src_path_buf[PATH_MAX_LENGTH] = {0};
   char dst_path_buf[PATH_MAX_LENGTH] = {0};

   fill_pathname_join(src_path_buf, g_defaults.dirs[DEFAULT_DIR_PORT], "media", sizeof(g_defaults.dirs[DEFAULT_DIR_PORT]));
   fill_pathname_join(dst_path_buf, g_defaults.dirs[DEFAULT_DIR_PORT], "assets", sizeof(g_defaults.dirs[DEFAULT_DIR_PORT]));

   if (exists(dst_path_buf) || !exists(src_path_buf))
      return;

   rename(src_path_buf, dst_path_buf);
}

static void frontend_wiiu_get_env_settings(int *argc, char *argv[],
      void *args, void *params_data)
{
   if (have_libfat_sdcard)
      strncpy(g_defaults.dirs[DEFAULT_DIR_PORT], WIIU_SD_FAT_PATH "retroarch/", sizeof(g_defaults.dirs[DEFAULT_DIR_PORT]));
   else
      strncpy(g_defaults.dirs[DEFAULT_DIR_PORT], WIIU_SD_PATH "retroarch/", sizeof(g_defaults.dirs[DEFAULT_DIR_PORT]));

   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE_ASSETS], g_defaults.dirs[DEFAULT_DIR_PORT],
         "downloads", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE_ASSETS]));
   fix_asset_directory();
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_ASSETS], g_defaults.dirs[DEFAULT_DIR_PORT],
         "assets", sizeof(g_defaults.dirs[DEFAULT_DIR_ASSETS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE], g_defaults.dirs[DEFAULT_DIR_PORT],
         "cores", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE_INFO], g_defaults.dirs[DEFAULT_DIR_CORE],
         "info", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE_INFO]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SAVESTATE], g_defaults.dirs[DEFAULT_DIR_CORE],
         "savestates", sizeof(g_defaults.dirs[DEFAULT_DIR_SAVESTATE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SRAM], g_defaults.dirs[DEFAULT_DIR_CORE],
         "savefiles", sizeof(g_defaults.dirs[DEFAULT_DIR_SRAM]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SYSTEM], g_defaults.dirs[DEFAULT_DIR_CORE],
         "system", sizeof(g_defaults.dirs[DEFAULT_DIR_SYSTEM]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_PLAYLIST], g_defaults.dirs[DEFAULT_DIR_CORE],
         "playlists", sizeof(g_defaults.dirs[DEFAULT_DIR_PLAYLIST]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_MENU_CONFIG], g_defaults.dirs[DEFAULT_DIR_PORT],
         "config", sizeof(g_defaults.dirs[DEFAULT_DIR_MENU_CONFIG]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_REMAP], g_defaults.dirs[DEFAULT_DIR_PORT],
         "config/remaps", sizeof(g_defaults.dirs[DEFAULT_DIR_REMAP]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_VIDEO_FILTER], g_defaults.dirs[DEFAULT_DIR_PORT],
         "filters", sizeof(g_defaults.dirs[DEFAULT_DIR_VIDEO_FILTER]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_DATABASE], g_defaults.dirs[DEFAULT_DIR_PORT],
         "database/rdb", sizeof(g_defaults.dirs[DEFAULT_DIR_DATABASE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_LOGS], g_defaults.dirs[DEFAULT_DIR_CORE],
         "logs", sizeof(g_defaults.dirs[DEFAULT_DIR_LOGS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_THUMBNAILS], g_defaults.dirs[DEFAULT_DIR_PORT],
         "thumbnails", sizeof(g_defaults.dirs[DEFAULT_DIR_THUMBNAILS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_OVERLAY], g_defaults.dirs[DEFAULT_DIR_PORT],
         "overlays", sizeof(g_defaults.dirs[DEFAULT_DIR_OVERLAY]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SCREENSHOT], g_defaults.dirs[DEFAULT_DIR_PORT],
         "screenshots", sizeof(g_defaults.dirs[DEFAULT_DIR_SCREENSHOT]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_AUTOCONFIG], g_defaults.dirs[DEFAULT_DIR_PORT],
         "autoconfig", sizeof(g_defaults.dirs[DEFAULT_DIR_AUTOCONFIG]));
   fill_pathname_join(g_defaults.path_config, g_defaults.dirs[DEFAULT_DIR_PORT],
         FILE_PATH_MAIN_CONFIG, sizeof(g_defaults.path_config));

#ifndef IS_SALAMANDER
   dir_check_defaults("custom.ini");
#endif
}

static void frontend_wiiu_deinit(void *data)
{
   (void)data;
}

static void frontend_wiiu_shutdown(bool unused)
{
   (void)unused;
}

static void frontend_wiiu_init(void *data)
{
   (void)data;
   DEBUG_LINE();
   verbosity_enable();
   DEBUG_LINE();
}

static int frontend_wiiu_get_rating(void) { return 10; }

enum frontend_architecture frontend_wiiu_get_arch(void)
{
   return FRONTEND_ARCH_PPC;
}

static int frontend_wiiu_parse_drive_list(void *data, bool load_content)
{
#ifndef IS_SALAMANDER
   file_list_t *list = (file_list_t *)data;
   enum msg_hash_enums enum_idx = load_content ?
      MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR :
      MENU_ENUM_LABEL_FILE_BROWSER_DIRECTORY;

   if (!list)
      return -1;


   if (have_libfat_sdcard)
      menu_entries_append(list, WIIU_SD_FAT_PATH,
                          msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
                          enum_idx,
                          FILE_TYPE_DIRECTORY, 0, 0, NULL);
   else
      menu_entries_append(list, WIIU_SD_PATH,
                          msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
                          enum_idx,
                          FILE_TYPE_DIRECTORY, 0, 0, NULL);

   if (have_libfat_usb)
       menu_entries_append(list, WIIU_USB_FAT_PATH,
                           msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
                           enum_idx,
                           FILE_TYPE_DIRECTORY, 0, 0, NULL);

#endif
   return 0;
}

static void frontend_wiiu_exec(const char *path, bool should_load_content)
{
    /* goal: make one big buffer with all the argv's, seperated by NUL. we can then pass this thru sysapp! */
    char* argv_buf;
    size_t n, argv_len = strlen(path) + 1; /* argv[0] plus null */

#ifndef IS_SALAMANDER
    const char *content = path_get(RARCH_PATH_CONTENT);
    const char *content_args[2] = {content, NULL };
#ifdef HAVE_NETWORKING
    const char *netplay_args[NETPLAY_FORK_MAX_ARGS];
#endif
#endif
    /* args will select between content_args, netplay_args, or no args (default) */
    const char **args = NULL;

    /* and some other stuff (C89) */
    MochaRPXLoadInfo load_info = { 0 };
    MochaUtilsStatus ret;
    SYSStandardArgsIn std_args = { 0 };

#ifndef IS_SALAMANDER
    if (should_load_content)
    {
#ifdef HAVE_NETWORKING
        if (netplay_driver_ctl(RARCH_NETPLAY_CTL_GET_FORK_ARGS, (void*)netplay_args))
        {
            const char **cur_arg = netplay_args;

            do
                argv_len += strnlen(*cur_arg, PATH_MAX_LENGTH) + 1;
            while (*(++cur_arg));

            args = netplay_args;
        }
        else
#endif
        if (!string_is_empty(content))
        {
            argv_len += strnlen(content, PATH_MAX_LENGTH) + 1;
            args = content_args;
        }
    }
#endif

    argv_buf = malloc(argv_len);
    argv_buf[0] = '\0';

    n = strlcpy(argv_buf, path, argv_len);
    n++; /* leave room for the NUL */
    if (args)
    {
        const char **cur_arg = args;
        do {
            n += strlcpy(argv_buf + n, *cur_arg, argv_len - n);
            n++;
        } while (*(++cur_arg));
    }

    if (string_starts_with(path, "fs:/vol/external01/"))
        path_relative_to(load_info.path, path, "fs:/vol/external01/", sizeof(load_info.path));
    else if (string_starts_with(path, "sd:/"))
        path_relative_to(load_info.path, path, "sd:/", sizeof(load_info.path));
    else goto cleanup; /* bail if not on the SD card */

    /* Mocha might not be init'd (Salamander) */
    if (Mocha_InitLibrary() != MOCHA_RESULT_SUCCESS)
        goto cleanup;

    load_info.target = LOAD_RPX_TARGET_SD_CARD;
    ret = Mocha_PrepareRPXLaunch(&load_info);
    if (ret != MOCHA_RESULT_SUCCESS)
        goto cleanup;

    std_args.argString = argv_buf;
    std_args.size = argv_len;
    ret = Mocha_LaunchHomebrewWrapperEx(&std_args);
    if (ret != MOCHA_RESULT_SUCCESS)
    {
        MochaRPXLoadInfo load_info_revert;
        load_info_revert.target = LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE;
        Mocha_PrepareRPXLaunch(&load_info_revert);
        goto cleanup;
    }

    in_exec = true;

cleanup:
    free(argv_buf);
    argv_buf = NULL;
}

static void frontend_wiiu_exitspawn(char *s, size_t len, char *args)
{
    bool should_load_content = false;
#ifndef IS_SALAMANDER
    if (wiiu_fork_mode == FRONTEND_FORK_NONE)
        return;

    switch (wiiu_fork_mode)
    {
        case FRONTEND_FORK_CORE_WITH_ARGS:
            should_load_content = true;
            break;
        default:
            break;
    }
#endif
    frontend_wiiu_exec(s, should_load_content);
}

#ifndef IS_SALAMANDER
static bool frontend_wiiu_set_fork(enum frontend_fork fork_mode)
{
    switch (fork_mode)
    {
        case FRONTEND_FORK_CORE:
            wiiu_fork_mode  = fork_mode;
            break;
        case FRONTEND_FORK_CORE_WITH_ARGS:
            wiiu_fork_mode  = fork_mode;
            break;
        case FRONTEND_FORK_RESTART:
            /* NOTE: We don't implement Salamander, so just turn
             * this into FRONTEND_FORK_CORE. */
            wiiu_fork_mode  = FRONTEND_FORK_CORE;
            break;
        case FRONTEND_FORK_NONE:
        default:
            return false;
    }

    return true;
}
#endif

frontend_ctx_driver_t frontend_ctx_wiiu =
{
   frontend_wiiu_get_env_settings,
   frontend_wiiu_init,
   frontend_wiiu_deinit,
   frontend_wiiu_exitspawn,
   NULL,                         /* process_args */
   frontend_wiiu_exec,
#ifdef IS_SALAMANDER
   NULL,                         /* set_fork */
#else
   frontend_wiiu_set_fork,
#endif
   frontend_wiiu_shutdown,
   NULL,                         /* get_name */
   NULL,                         /* get_os */
   frontend_wiiu_get_rating,
   NULL,                         /* content_loaded */
   frontend_wiiu_get_arch,       /* get_architecture */
   NULL,                         /* get_powerstate */
   frontend_wiiu_parse_drive_list,
   NULL,                         /* get_total_mem */
   NULL,                         /* get_free_mem */
   NULL,                         /* install_signal_handler */
   NULL,                         /* get_signal_handler_state */
   NULL,                         /* set_signal_handler_state       */
   NULL,                         /* destroy_signal_handler_state   */
   NULL,                         /* attach_console                 */
   NULL,                         /* detach_console                 */
   NULL,                         /* get_lakka_version              */
   NULL,                         /* set_screen_brightness          */
   NULL,                         /* watch_path_for_changes         */
   NULL,                         /* check_for_path_changes         */
   NULL,                         /* set_sustained_performance_mode */
   NULL,                         /* get_cpu_model_name             */
   NULL,                         /* get_user_language              */
   NULL,                         /* is_narrator_running            */
   NULL,                         /* accessibility_speak            */
   NULL,                         /* set_gamemode                   */
   "wiiu",                       /* ident                          */
   NULL                          /* get_video_driver               */
};

/* main() and its supporting functions */

static void main_setup(void);
static void get_arguments(int *argc, char ***argv);
#ifndef IS_SALAMANDER
static void main_loop(void);
#endif
static void main_teardown(void);
static void init_filesystems(void);
static void deinit_filesystems(void);
static void init_logging(void);
static void deinit_logging(void);
static ssize_t wiiu_log_write(struct _reent *r, void *fd, const char *ptr, size_t len);
static void init_pad_libraries(void);
static void deinit_pad_libraries(void);
static void proc_setup(void);
static void proc_exit(void);
static void proc_save_callback(void);

int main(int argc, char **argv)
{
   proc_setup();
   main_setup();
   get_arguments(&argc, &argv);

#ifdef IS_SALAMANDER
   int salamander_main(int argc, char **argv);
   salamander_main(argc, argv);
#else
   rarch_main(argc, argv, NULL);
   main_loop();
   main_exit(NULL);
#endif /* IS_SALAMANDER */
   main_teardown();

   proc_exit();
   /* We always return 0 because if we don't, it can prevent loading a
    * different RPX/ELF in HBL. */
   return 0;
}

static void main_setup(void)
{
   memoryInitialize();
   init_os_exceptions();
   init_logging();
   init_filesystems();
   init_pad_libraries();
   verbosity_enable();
   fflush(stdout);
}

static void main_teardown(void)
{
   deinit_pad_libraries();
   deinit_filesystems();
   deinit_logging();
   deinit_os_exceptions();
   memoryRelease();
}

// https://github.com/devkitPro/wut/blob/7d9fa9e416bffbcd747f1a8e5701fd6342f9bc3d/libraries/libwhb/src/proc.c

#define HBL_TITLE_ID (0x0005000013374842)
#define MII_MAKER_JPN_TITLE_ID (0x000500101004A000)
#define MII_MAKER_USA_TITLE_ID (0x000500101004A100)
#define MII_MAKER_EUR_TITLE_ID (0x000500101004A200)

static bool in_aroma = false;
static bool in_hbl = false;
static void proc_setup(void)
{
    uint64_t titleID = OSGetTitleID();

    // Homebrew Launcher does not like the standard ProcUI application loop, sad!
    if (titleID == HBL_TITLE_ID ||
        titleID == MII_MAKER_JPN_TITLE_ID ||
        titleID == MII_MAKER_USA_TITLE_ID ||
        titleID == MII_MAKER_EUR_TITLE_ID)
    {
        // Important: OSEnableHomeButtonMenu must come before ProcUIInitEx.
        OSEnableHomeButtonMenu(FALSE);
        in_hbl = TRUE;
    }

    /* Detect Aroma explicitly (it's possible to run under H&S while using Tiramisu) */
    OSDynLoad_Module rpxModule;
    if (OSDynLoad_Acquire("homebrew_rpx_loader", &rpxModule) == OS_DYNLOAD_OK)
    {
        in_aroma = true;
        OSDynLoad_Release(rpxModule);
    }

    ProcUIInit(&proc_save_callback);
}

static void proc_exit(void)
{
    /* If we're doing a normal exit while running under HBL, we must SYSRelaunchTitle.
     * If we're in an exec (i.e. launching mocha homebrew wrapper) we must *not* do that. yay! */
    if (in_hbl && !in_exec)
        SYSRelaunchTitle(0, NULL);

    /* Similar deal for Aroma, but exit to menu. */
    if (!in_hbl && !in_exec)
        SYSLaunchMenu();

    /* Now just tell the OS that we really are ok to exit */
    if (!ProcUIInShutdown())
    {
        for (;;)
        {
            ProcUIStatus status;
            status = ProcUIProcessMessages(TRUE);
            if (status == PROCUI_STATUS_EXITING)
                break;
            else if (status == PROCUI_STATUS_RELEASE_FOREGROUND)
                ProcUIDrawDoneRelease();
        }
    }

    ProcUIShutdown();
}

static void proc_save_callback(void)
{
    OSSavesDone_ReadyToRelease();
}

static void sysapp_arg_cb(SYSDeserializeArg* arg, void* usr)
{
    SYSStandardArgs *std_args = (SYSStandardArgs *)usr;

    if (_SYSDeserializeStandardArg(arg, std_args))
        return;

    if (strcmp(arg->argName, "sys:pack") == 0) {
        // Recurse
        SYSDeserializeSysArgsFromBlock(arg->data, arg->size, sysapp_arg_cb, usr);
        return;
    }
}

static void get_arguments(int *argc, char ***argv)
{
#ifdef HAVE_NETWORKING
    static char* _argv[1 + NETPLAY_FORK_MAX_ARGS];
#else
    static char* _argv[2];
#endif
    int _argc = 0;
    SYSStandardArgs std_args = { 0 };

    /* we could do something more rich with the content path and things here - but since there's not a great way
     * to actually pass that info along to RA, just emulate argc/argv */
    SYSDeserializeSysArgs(sysapp_arg_cb, &std_args);

    char* argv_buf = std_args.anchorData;
    size_t argv_len = std_args.anchorSize;
    if (!argv_buf || argv_len == 0)
        return;

    size_t n = 0;
    while (n < argv_len && _argc < ARRAY_SIZE(_argv)) {
        char* s = argv_buf + n;
        _argv[_argc++] = s;
        n += strlen(s);
        n++; /* skip the null */
    }

    *argc = _argc;
    *argv = _argv;
}

#ifndef IS_SALAMANDER
static bool swap_is_pending(void *start_time)
{
   uint32_t swap_count, flip_count;
   OSTime last_flip, last_vsync;

   GX2GetSwapStatus(&swap_count, &flip_count, &last_flip, &last_vsync);
   return last_vsync < *(OSTime *)start_time;
}

static void main_loop(void)
{
   OSTime start_time;
   int status;

   for (;;)
   {
      if (video_driver_get_ptr())
      {
         start_time = OSGetSystemTime();
         task_queue_wait(swap_is_pending, &start_time);
      }
      else
         task_queue_wait(NULL, NULL);

      status = runloop_iterate();

      if (status == -1)
         break;
   }
}
#endif

static void init_filesystems(void)
{
#if defined(HAVE_LIBMOCHA) && defined(HAVE_LIBFAT)
    if (Mocha_InitLibrary() == MOCHA_RESULT_SUCCESS)
    {
        have_libfat_usb    = fatMount("usb", &Mocha_usb_disc_interface, 0, 512, 128);
       /* Mounting SD card with libfat is unsafe under Aroma */
       if (!in_aroma)
          have_libfat_sdcard = fatMount("sd", &Mocha_sdio_disc_interface, 0, 512, 128);
    }
#endif
}

static void deinit_filesystems(void)
{
#if defined(HAVE_LIBMOCHA) && defined(HAVE_LIBFAT)
   if (have_libfat_usb)
      fatUnmount("usb");
   if (have_libfat_sdcard)
      fatUnmount("sd");

   Mocha_DeInitLibrary();
#endif
}

static devoptab_t dotab_stdout =
{
    "stdout_whb",   /* device name */
    0,              /* size of file structure */
    NULL,           /* device open */
    NULL,           /* device close */
    wiiu_log_write, /* device write */
    NULL,           /* ... */
};

static void init_logging(void)
{
   WHBLogUdpInit();
   WHBLogCafeInit();
   devoptab_list[STD_OUT] = &dotab_stdout;
   devoptab_list[STD_ERR] = &dotab_stdout;
}

static void deinit_logging(void)
{
   fflush(stdout);
   fflush(stderr);

   WHBLogCafeDeinit();
   WHBLogUdpDeinit();
}

static ssize_t wiiu_log_write(struct _reent *r,
                              void *fd, const char *ptr, size_t len)
{
    // Do a bit of line buffering to try and make the log output nicer
    // we just truncate if a line goes over
    static char linebuf[2048]; //match wut's PRINTF_BUFFER_LENGTH
    static size_t linebuf_pos = 0;

    snprintf(linebuf + linebuf_pos, sizeof(linebuf) - linebuf_pos - 1, "%.*s", len, ptr);
    linebuf_pos = strlen(linebuf);

    if (linebuf[linebuf_pos - 1] == '\n' || linebuf_pos >= sizeof(linebuf) - 2) {
        WHBLogWrite(linebuf);
        linebuf_pos = 0;
    }

    return (ssize_t)len;
}

static void init_pad_libraries(void)
{
#ifndef IS_SALAMANDER
   KPADInit();
   WPADEnableURCC(true);
   WPADEnableWiiRemote(true);
#endif /* IS_SALAMANDER */
}

static void deinit_pad_libraries(void)
{
#ifndef IS_SALAMANDER
   KPADShutdown();
#endif /* IS_SALAMANDER */
}
