/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * NiOn AppImage WebKit subprocess shim.
 *
 * Distribution WebKitGTK builds commonly compile absolute paths for
 * WebKitWebProcess/WebKitNetworkProcess/WebKitGPUProcess. AppImages are mounted
 * at a runtime path, so those host absolute paths are not portable. The NiOn
 * launcher preloads this tiny shim only into the NiOn UI process. It rewrites
 * WebKit helper executable paths to NION_WEBKIT_EXEC_DIR while leaving all
 * other exec/spawn calls untouched.
 *
 * WebKitGTK 6's WebProcess sandbox remains enabled. NiOn separately adds the
 * AppDir mount to WebKitWebContext's sandbox allow-list before any WebProcess
 * is created, so rewritten helper paths remain visible inside bubblewrap.
 */

extern char **environ;

static int is_webkit_helper(const char *path)
{
    if (!path || !*path)
        return 0;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strcmp(base, "WebKitWebProcess") == 0 ||
           strcmp(base, "WebKitNetworkProcess") == 0 ||
           strcmp(base, "WebKitGPUProcess") == 0;
}

static char *rewrite_one(const char *path)
{
    if (!is_webkit_helper(path))
        return NULL;

    const char *dir = getenv("NION_WEBKIT_EXEC_DIR");
    if (!dir || !*dir)
        return NULL;

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t need = strlen(dir) + 1 + strlen(base) + 1;
    char *candidate = malloc(need);
    if (!candidate)
        return NULL;
    snprintf(candidate, need, "%s/%s", dir, base);
    if (access(candidate, X_OK) != 0) {
        free(candidate);
        return NULL;
    }
    return candidate;
}

static char **rewrite_argv(char *const argv[], char ***owned_out)
{
    *owned_out = NULL;
    if (!argv)
        return (char **)argv;

    size_t count = 0;
    while (argv[count])
        count++;

    char **copy = calloc(count + 1, sizeof(char *));
    if (!copy)
        return (char **)argv;

    int changed = 0;
    for (size_t i = 0; i < count; i++) {
        char *replacement = rewrite_one(argv[i]);
        if (replacement) {
            copy[i] = replacement;
            changed = 1;
        } else {
            copy[i] = argv[i];
        }
    }
    copy[count] = NULL;

    if (!changed) {
        free(copy);
        return (char **)argv;
    }

    *owned_out = copy;
    return copy;
}

static void free_rewritten_argv(char **owned, char *const original[])
{
    if (!owned)
        return;
    for (size_t i = 0; owned[i]; i++) {
        if (!original || owned[i] != original[i])
            free(owned[i]);
    }
    free(owned);
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[], char *const envp[])
{
    static int (*real_posix_spawn)(pid_t *, const char *,
        const posix_spawn_file_actions_t *, const posix_spawnattr_t *,
        char *const[], char *const[]) = NULL;
    if (!real_posix_spawn)
        real_posix_spawn = dlsym(RTLD_NEXT, "posix_spawn");
    if (!real_posix_spawn)
        return ENOSYS;

    char *new_path = rewrite_one(path);
    char **owned_argv = NULL;
    char **new_argv = rewrite_argv(argv, &owned_argv);
    int rc = real_posix_spawn(pid, new_path ? new_path : path,
                              file_actions, attrp, new_argv, envp);
    free_rewritten_argv(owned_argv, argv);
    free(new_path);
    return rc;
}

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const argv[], char *const envp[])
{
    static int (*real_posix_spawnp)(pid_t *, const char *,
        const posix_spawn_file_actions_t *, const posix_spawnattr_t *,
        char *const[], char *const[]) = NULL;
    if (!real_posix_spawnp)
        real_posix_spawnp = dlsym(RTLD_NEXT, "posix_spawnp");
    if (!real_posix_spawnp)
        return ENOSYS;

    char *new_file = rewrite_one(file);
    char **owned_argv = NULL;
    char **new_argv = rewrite_argv(argv, &owned_argv);
    int rc = real_posix_spawnp(pid, new_file ? new_file : file,
                               file_actions, attrp, new_argv, envp);
    free_rewritten_argv(owned_argv, argv);
    free(new_file);
    return rc;
}

int execve(const char *pathname, char *const argv[], char *const envp[])
{
    static int (*real_execve)(const char *, char *const[], char *const[]) = NULL;
    if (!real_execve)
        real_execve = dlsym(RTLD_NEXT, "execve");
    if (!real_execve) {
        errno = ENOSYS;
        return -1;
    }

    char *new_path = rewrite_one(pathname);
    char **owned_argv = NULL;
    char **new_argv = rewrite_argv(argv, &owned_argv);
    int rc = real_execve(new_path ? new_path : pathname, new_argv, envp);
    int saved_errno = errno;
    free_rewritten_argv(owned_argv, argv);
    free(new_path);
    errno = saved_errno;
    return rc;
}
