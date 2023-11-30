/*
 * Copyright (C) 2023 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 */

#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "nvidia-installer.h"
#include "user-interface.h"
#include "kernel.h"
#include "initramfs.h"
#include "misc.h"
#include "conflicting-kernel-modules.h"

/*
 * find_initramfs_images() - Locate initramfs image files whose names conform
 * to well-known patterns. Returns the number of located image files. If the
 * caller supplies a pointer to an array of strings, the list of found images
 * will be returned in a heap-allocated NULL-terminated list via that pointer.
 */
static int find_initramfs_images(Options *op, char ***found_paths)
{
    char *kernel_name  = get_kernel_name(op);
    int num_found_paths = 0;

    if (found_paths) {
        *found_paths = NULL;
    }

    if (kernel_name) {
        /* This must be at least two more than the number of times the
         * __TEST_INITRAMFS_FILE macro is invoked below. One for a NULL
         * terminator, and another for a "none of the above" option that
         * may be added by a caller. */
        const int found_paths_size = 8;
        char *tmp;

        if (found_paths) {
            *found_paths = nvalloc(found_paths_size * sizeof(char*));
        }

        #define __TEST_INITRAMFS_FILE(format, str) { \
            char *path = nvasprintf("/boot/" format, str); \
            if (access(path, F_OK) == 0) { \
                if (found_paths && num_found_paths < found_paths_size - 2) { \
                    (*found_paths)[num_found_paths] = path; \
                } \
                num_found_paths++; \
            } else { \
                nvfree(path); \
            } \
        }

        /* Don't forget to increase found_paths_size, if necessary, when
         * adding additional templates. */
        __TEST_INITRAMFS_FILE("initramfs-%s.img", kernel_name);
        __TEST_INITRAMFS_FILE("initramfs-%s.img", "linux");
        __TEST_INITRAMFS_FILE("initramfs-%s.img", "linux-lts");
        __TEST_INITRAMFS_FILE("initrd-%s", kernel_name);
        __TEST_INITRAMFS_FILE("initrd.img-%s", kernel_name);

        tmp = strchr(kernel_name, '.');
        if (tmp) {
            char *kernel_name_copy = strdup(kernel_name);

            if (kernel_name_copy) {
                tmp = strchr(kernel_name_copy, '.');
                if (tmp) {
                    tmp = strchr(tmp + 1, '.');
                }
                if (tmp) {
                    char *linux_ver_arch;

                    tmp[0] = '\0';
                    linux_ver_arch = nvstrcat("linux-", kernel_name_copy, "-",
                                              get_machine_arch(op), NULL);
                    __TEST_INITRAMFS_FILE("initramfs-%s.img", linux_ver_arch);
                    nvfree(linux_ver_arch);
                }
            }

            nvfree(kernel_name_copy);
        }
    }

    return num_found_paths;
}

/*
 * get_initramfs_path() - Test well-known locations for the existence of
 * candidate initramfs files. If there is more than one candidate, optionally
 * prompt the user to select one of them. Returns the found (if one) or
 * selected (if more than one found, and function is run interactively) file,
 * or NULL if no file is found or multiple candidates exist and the user does
 * not select one (either because the function is run non-interactively or the
 * user declines to select one when prompted.
 */

static char *get_initramfs_path(Options *op, int interactive)
{
    static char *path_ret = NULL;
    static int attempted = FALSE;
    int num_found_paths;
    char **found_paths;
    int i;

    if (attempted) {
        /* This function has already been called: return the cached path value
         * (which may be NULL if no path was found previously). */

        return path_ret;
    }

    num_found_paths = find_initramfs_images(op, &found_paths);

    if (num_found_paths == 1) {
        path_ret = found_paths[0];
    } else if (num_found_paths > 1 && interactive) {
        int answer;

        /* We ensured we have enough space in find_initramfs_images() */
        found_paths[num_found_paths] = "Use none of these";

        answer = ui_multiple_choice(op, (const char * const*)found_paths,
                                    num_found_paths + 1, num_found_paths,
                                    "More than one initramfs file found. "
                                    "Which file would you like to use?");
        if (answer < num_found_paths) {
            /* answer == found_paths means the user opted out */
            path_ret = found_paths[answer];
        }
    }

    /* Clean up the paths that we're not returning */
    for (i = 0; i < num_found_paths; i++) {
        if (found_paths[i] != path_ret) {
            nvfree(found_paths[i]);
        }
    }
    nvfree(found_paths);

    /* If a path is not found in non-interactive mode, allow trying again in
     * interactive mode later. */
    attempted = path_ret || interactive;

    return path_ret;
}

typedef enum {
    INITRAMFS_LIST_TOOL,
    INITRAMFS_REBUILD_TOOL,
} InitramfsToolType;

char *initramfs_tool_purpose[] = {
    [INITRAMFS_LIST_TOOL] = "listing initramfs contents",
    [INITRAMFS_REBUILD_TOOL] = "rebuilding initramfs",
};

/* Table of known initramfs tools and their arguments */
static struct initramfs_tool {
        /* Purpose of the tool (listing, rebuilding, etc.) */
        InitramfsToolType type;
        /* Name of the tool */
        const char *name;
        /* Arguments that are always set, for the purposes of nvidia-installer
         * using the tool. */
        const char *common_args;
        /* Arguments that are set when a kernel version is specified. These
         * arguments precede the kernel version. Set to NULL if the tool does
         * not accept kernel versions on its command line. */
        const char *kernel_specific_args;
        /* Indicates whether kernel version must be specified with this tool */
        int requires_kernel;
        /* Arguments that are set when an initramfs file is specified. These
         * arguments precede the path. Set to NULL if the tool does not accept
         * an initramfs file path on its command line. */
        const char *path_specific_args;
        /* Indicates whether initramfs path must be specified with this tool */
        int requires_path;
    } initramfs_tools[] = {
    {
        .type = INITRAMFS_LIST_TOOL,
        .name = "lsinitramfs",
        .common_args = "",
        .kernel_specific_args = NULL,
        .requires_kernel = FALSE,
        .path_specific_args = "-l",
        .requires_path = TRUE,
    },
    {
        .type = INITRAMFS_LIST_TOOL,
        .name = "lsinitrd",
        .common_args = "",
        .kernel_specific_args = "-k",
        .requires_kernel = FALSE,
        .path_specific_args = "",
        .requires_path = FALSE,
    },
    {
        .type = INITRAMFS_LIST_TOOL,
        .name = "lsinitcpio",
        .common_args = "",
        .kernel_specific_args = NULL,
        .requires_kernel = FALSE,
        .path_specific_args = "",
        .requires_path = TRUE,
    },
    {
        .type = INITRAMFS_REBUILD_TOOL,
        .name = "dracut",
        .common_args = "--force",
        .kernel_specific_args = "--kver",
        .requires_kernel = FALSE,
        .path_specific_args = "",
        .requires_path = FALSE,
    },
    {
        .type = INITRAMFS_REBUILD_TOOL,
        .name = "update-initramfs",
        .common_args = "-u",
        .kernel_specific_args = "-k",
        .requires_kernel = FALSE,
        .path_specific_args = NULL,
        .requires_path = FALSE,
    },
    {
        .type = INITRAMFS_REBUILD_TOOL,
        .name = "mkinitcpio",
        .common_args = "-P",
        .kernel_specific_args = "",
        .requires_kernel = FALSE,
        .path_specific_args = "",
        .requires_path = FALSE,
    },
};

typedef struct initramfs_tool InitramfsTool;

static int get_tool_index(Options *op, const InitramfsTool *tool)
{
    if (tool->requires_kernel && get_kernel_name(op) == NULL) {
        return -1;
    }

    if (tool->requires_path && find_initramfs_images(op, NULL) == 0) {
        return -1;
    }

    return tool - initramfs_tools;
}

enum {
    NON_INTERACTIVE,
    INTERACTIVE,
};

/* find_initramfs_tool() - Attempt to locate a tool of the given type.
 * Returns the index to the found tool's entry in the initramfs_tools[] table,
 * if exactly one matching tool is found. If more than one tool is found and
 * interactive is set, the user will be prompted to select one of the found
 * tools. Returns the index to the found or selected tool, or -1 if no tool is
 * found, or if a multiple-tool situation is unresolved, either by user choice
 * or when running in non-interactive mode. */
static int find_initramfs_tool(Options *op, InitramfsToolType type,
                               int interactive)
{
    InitramfsTool *found_tools[ARRAY_LEN(initramfs_tools)];
    const char *purpose = initramfs_tool_purpose[type];
    int i, num_found_tools = 0;

    for (i = 0; i < ARRAY_LEN(initramfs_tools); i++) {
        if (initramfs_tools[i].type != type) {
            continue;
        }

        if (find_system_util(initramfs_tools[i].name)) {
            found_tools[num_found_tools++] = initramfs_tools + i;
        }
    }

    if (num_found_tools == 0) {
        ui_log(op, "Unable to locate any tools for %s.", purpose);
        return -1;
    }

    if (num_found_tools == 1) {
        return get_tool_index(op, found_tools[0]);
    } else if (interactive == NON_INTERACTIVE)  {
        return -1;
    } else {
        const char *found_tool_names[num_found_tools + 1];
        int chosen_tool;

        for (i = 0; i < num_found_tools; i++) {
            found_tool_names[i] = found_tools[i]->name;
        }

        found_tool_names[num_found_tools] = "None of these";

        chosen_tool = ui_multiple_choice(op, found_tool_names,
                                         num_found_tools + 1, num_found_tools,
                                         "More than one tool for %s detected. "
                                         "Which tool would you like to use?",
                                         purpose);

        if (chosen_tool == num_found_tools) {
            return -1;
        }

        return get_tool_index(op, found_tools[chosen_tool]);
    }
}

/*
 * initramfs_tool_helper() - run the specified tool with the given kernel and
 * initramfs file path arguments.
 */
static int initramfs_tool_helper(Options *op, int tool, const char *kernel,
                                 const char *path, char **data, int interactive)
{
    char *cmd, *tool_path, *kernel_args, *path_args, *buf, **cmd_data;
    int standalone = !op->ui.status_active;
    int ret = 1;

    if (data) {
        cmd_data = data;
    } else {
        cmd_data = &buf;
    }

    *cmd_data = NULL;

    tool_path = find_system_util(initramfs_tools[tool].name);
    kernel_args = path_args = NULL;

    /* Build kernel version specific command line arguments */
    if (kernel) {
        if (!initramfs_tools[tool].kernel_specific_args) {
            ui_log(op, "%s does not take a kernel argument.", tool_path);
            goto done;
        }
        kernel_args = nvstrcat(initramfs_tools[tool].kernel_specific_args, " ",
                               kernel, NULL);
    } else {
        if (initramfs_tools[tool].requires_kernel) {
            ui_log(op, "%s requires a kernel argument, but none was given.",
                   tool_path);
            goto done;
        }
        kernel_args = nvstrdup("");
    }

    /* Build initramfs file path specific command line arguments */
    if (path && initramfs_tools[tool].path_specific_args) {
        if (!initramfs_tools[tool].path_specific_args) {
            ui_log(op, "%s does not take a path argument.", tool_path);
            goto done;
        }
        path_args = nvstrcat(initramfs_tools[tool].path_specific_args, " ",
                             path, NULL);
    } else {
        if (initramfs_tools[tool].requires_path || !path) {
            ui_log(op, "%s requires a file path argument, but none was given.",
                   tool_path);
            goto done;
        }
        path_args = nvstrdup("");
    }

    /* Combine arguments into full command line. */
    cmd = nvstrcat(tool_path, " ", initramfs_tools[tool].common_args, " ",
                   kernel_args, " ", path_args, NULL);

    if (interactive) {
        const char *s = initramfs_tool_purpose[initramfs_tools[tool].type];

        if (standalone) {
            ui_status_begin(op, "Processing the initramfs:", "%s", s);
        }

        ui_indeterminate_begin(op, "%s (this may take a while)", s);
    }

    ui_log(op, "Executing: %s", cmd);
    ret = run_command(op, cmd_data, FALSE, NULL, TRUE, cmd, NULL);

    if (interactive) {
        ui_indeterminate_end(op);

        if (standalone) {
            ui_status_end(op, ret == 0 ? "done" : "failed");
        }
    }

    if (ret != 0) {
        ui_log(op, "Failed to run `%s`:\n\n%s", cmd, *cmd_data);
    }
    nvfree(cmd);

done:
    nvfree(tool_path);
    nvfree(kernel_args);
    nvfree(path_args);

    if (!data) {
        nvfree(buf);
    }

    return ret;
}


/* run_initramfs_tool() - attempt to run the specified tool with the minimum
 * required command line arguments, falling back to more explicitly specified
 * command line arguments upon failure. */
static int run_initramfs_tool(Options *op, int tool, char **data,
                              int interactive)
{
    int ret = -1;
    /* Always specify the kernel version if the tool requires it, or if the
     * installer has an explicit kernel version set, which typically indicates
     * that a kernel other than the currently running one is being targeted. */
    int kernel_required = initramfs_tools[tool].requires_kernel ||
                          op->kernel_name != NULL;

    if (data) {
        *data = NULL;
    }

    /* Run without specifying kernel or initramfs path, if neither is required. */
    if (!kernel_required && !initramfs_tools[tool].requires_path) {
        ret = initramfs_tool_helper(op, tool, NULL, NULL, data, interactive);
    }

    /* Run with only the initramfs path, if a kernel is not required. */
    if (ret != 0 && !kernel_required) {
        char *initramfs_path = get_initramfs_path(op, interactive);

        if (data) {
            nvfree(*data);
        }
        ret = initramfs_tool_helper(op, tool, NULL, initramfs_path,
                                    data, interactive);
    }

    /* Run with only the kernel, if an initramfs path is not required. */
    if (ret != 0 && !initramfs_tools[tool].requires_path) {
        if (data) {
            nvfree(*data);
        }
        ret = initramfs_tool_helper(op, tool, get_kernel_name(op), NULL, data,
                                    interactive);
    }

    /* Run with both the kernel and initramfs path. */
    if (ret != 0) {
        char *initramfs_path = get_initramfs_path(op, interactive);

        if (data) {
            nvfree(*data);
        }
        ret = initramfs_tool_helper(op, tool, get_kernel_name(op),
                                    initramfs_path, data, interactive);
    }

    return ret;
}

static pthread_t scan_thread;

typedef struct {
    /* Index into initramfs_tools[] for the initramfs scanning tool. A negative
     * index indicates that no suitable tool was found. The index should be
     * initialized with either the return value of find_initramfs_tool() or a
     * negative value. */
    int tool;

    /* Flags to indicate the reults of an attempted initramfs scan. These flags
     * should all be zero-initialized before the first scan attempt. */

    /* Initramfs scan detected Nouveau in the initramfs */
    int nouveau_ko_detected;
    /* Initramfs scan detected NVIDIA kernel modules in the initramfs */
    int nvidia_ko_detected;
    /* A non-interactive scan was attempted, but interaction is required to
     * complete the scan (e.g. because the user needs to make a choice between
     * more than one available candidate tool). */
    int try_scan_again;
    /* The initramfs was successfully scanned and the *_ko_detected flags can
     * be trusted to accurately reflect the contents of the initramfs. */
    int scan_complete;
} ScanThreadData;

static void scan_initramfs(Options *op, ScanThreadData *data, int interactive)
{
    if (data->tool >= 0) {
        char *listing;
        int ret;

        ui_log(op, "Scanning the initramfs with %s...",
               initramfs_tools[data->tool].name);

        ret = run_initramfs_tool(op, data->tool, &listing, interactive);
        data->scan_complete = FALSE;

        if (ret == 0) {
            int i;

            if (strstr(listing, "/nouveau.ko")) {
                ui_log(op, "Nouveau detected in initramfs");

                data->nouveau_ko_detected = TRUE;
            }

            for (i = 0; i < num_conflicting_kernel_modules; i++) {
                char *module = nvstrcat("/", conflicting_kernel_modules[i],
                                        ".ko", NULL);
                if (strstr(listing, module)) {
                    ui_log(op, "%s detected in initramfs", module + 1);

                    data->nvidia_ko_detected = TRUE;
                }

                nvfree(module);

                if (data->nvidia_ko_detected) {
                    break;
                }
            }

            data->scan_complete = TRUE;
        }
        nvfree(listing);

        /* If the scan failed in non-interactive mode, we'll want to try again
         * in interactive mode later. */
        data->try_scan_again = !interactive && !data->scan_complete;
        ui_log(op, "Initramfs scan %s.", ret == 0 ? "complete" : "failed");
    } else {
        ui_log(op, "Unable to scan initramfs: no tool found");
    }
}

static void *initramfs_scan_worker(void *arg)
{
    static ScanThreadData data = {};
    Options *op = arg;

    data.tool = find_initramfs_tool(op, INITRAMFS_LIST_TOOL, NON_INTERACTIVE);

    scan_initramfs(op, &data, NON_INTERACTIVE);

    return &data;
}

int begin_initramfs_scan(Options *op)
{
    static int scan_started;
    int ret;

    if (scan_started) {
        return TRUE;
    }

    ret = pthread_create(&scan_thread, NULL, initramfs_scan_worker, op);

    if (ret == 0) {
        scan_started = TRUE;
        return TRUE;
    }

    return FALSE;
}

/* Attempt to detect conditions under which an initramfs rebuild may be useful,
 * and guide user through rebuilding if desired. Returns TRUE on success, or if
 * not rebuilding. Returns FALSE if a rebuild was attempted, but failed. */
int update_initramfs(Options *op)
{
    int rebuild_tool, ret = FALSE, pthread_join_ret;
    const char *no_listing = "Unable to determine whether NVIDIA kernel "
                             "modules are present in the initramfs. Existing "
                             "NVIDIA kernel modules in the initramfs, if any, "
                             "may interfere with the newly installed driver.";
    const char * const choices[] = {
                                       "Do not rebuild initramfs",
                                       "Rebuild initramfs"
                                   };
    ScanThreadData data = {}, *data_pointer;
    char *reason;

    rebuild_tool = find_initramfs_tool(op, INITRAMFS_REBUILD_TOOL, INTERACTIVE);

    /* Handle explicit user requests for initramfs rebuilding behavior */
    if (op->rebuild_initramfs != NV_OPTIONAL_BOOL_DEFAULT) {
        if (op->rebuild_initramfs == NV_OPTIONAL_BOOL_TRUE) {
            if (rebuild_tool >= 0) {
                ret = run_initramfs_tool(op, rebuild_tool, NULL, INTERACTIVE)
                      == 0;
            } else {
                ui_warn(op, "An initramfs rebuild was requested on the "
                            "installer command line, but a suitable tool was "
                            "not found.");
            }
        } else {
            ret = TRUE;
            ui_log(op, "Skipping initramfs rebuild.");
        }

        goto done;
    }

    pthread_join_ret = pthread_join(scan_thread, (void **) &data_pointer);

    if (pthread_join_ret != 0) {
        data_pointer = &data;
    }

    if (data_pointer->try_scan_again) {
        data_pointer->tool = find_initramfs_tool(op, INITRAMFS_LIST_TOOL,
                                                 INTERACTIVE);

        scan_initramfs(op, data_pointer, INTERACTIVE);
    }

    reason = nvstrdup("");

    if (nouveau_is_present()) {
        add_bullet_list_item("nvidia-installer attempted to disable Nouveau.",
                             &reason);
    }

    if (data_pointer->nouveau_ko_detected) {
        add_bullet_list_item("Nouveau is present in the initramfs.", &reason);
    }

    if (data_pointer->nvidia_ko_detected) {
        add_bullet_list_item("An NVIDIA kernel module was found in the "
                             "initramfs.", &reason);
    }

    /* If rebuilding tools were detected, ask user whether to rebuild. */
    if (rebuild_tool >= 0) {
        int rebuild = FALSE;

        if (reason[0]) {
            rebuild = ui_multiple_choice(op, choices, 2, 1,
                                         "The initramfs will likely need to be "
                                         "rebuilt due to the following "
                                         "condition(s):\n%s\n"
                                         "Would you like to rebuild the "
                                         "initramfs?", reason);
        } else if (data_pointer->scan_complete) {
            ui_log(op, "No NVIDIA modules detected in the initramfs.");
            ret = TRUE;
        } else {
            rebuild = ui_multiple_choice(op, choices, 2, 0,
                                         "%s Would you like to rebuild "
                                         "the initramfs?", no_listing);
        }

        if (rebuild) {
            ret = run_initramfs_tool(op, rebuild_tool, NULL, INTERACTIVE) == 0;

            if (!ret) {
                ui_error(op, "Failed to rebuild the initramfs!");
            }
        } else {
            ui_log(op, "The initramfs will not be rebuild.");
            ret = TRUE;
        }
    } else if (reason[0]) {
    /* Alert user that an initramfs rebuild may be needed, but tools to do so
     * were not detected.  */
        ui_warn(op, "nvidia-installer was unable to locate a tool for "
                    "rebuilding the initramfs, which is strongly recommended "
                    "due to the following condition(s):\n%s\n"
                    "Please consult your distribution's documentation for "
                    "instructions on how to rebuild the initramfs.", reason);
        ret = TRUE;
    } else if (!data_pointer->scan_complete) {
        ui_message(op, "%s", no_listing);
        ret = TRUE;
    }

    nvfree(reason);

done:
    return ret;
}
