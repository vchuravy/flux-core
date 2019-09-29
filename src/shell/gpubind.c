/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* GPU binding plugin
 *
 * Builtin GPU binding for flux-shell. Spread CUDA_VISIBLE_DEVICES
 *  across tasks depending on number in slot.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>
#include <flux/idset.h>

#include "src/common/libutil/log.h"
#include "builtins.h"

int ngpus_per_task = -1;

int get_shell_gpus (flux_shell_t *shell,
                    int *ntasks,
                    struct idset **ids)
{
    int rc = -1;
    char *s = NULL;
    const char *gpu_list = NULL;
    json_t *o = NULL;
    json_error_t err;
    struct idset *gpus = NULL;

    if (flux_shell_get_rank_info (shell, -1, &s) < 0) {
        log_err ("flux_shell_get_rank_info");
        return -1;
    }
    if (!(o = json_loads (s, 0, NULL))) {
        log_err ("json_loads");
        goto out;
    }
    if (json_unpack_ex (o, &err, 0, "{s:i s:{s?:s}}",
                                   "ntasks", ntasks,
                                   "resources",
                                     "gpus", &gpu_list) < 0) {
        log_err ("json_unpack: %s", err.text);
        goto out;
    }
    if (!(gpus = idset_decode (gpu_list ? gpu_list : ""))) {
        log_err ("idset_encode (%s)", gpu_list);
        goto out;
    }
    rc = 0;
out:
    free (s);
    json_decref (o);
    *ids = gpus;
    return rc;
}

static int plugin_task_setenv (flux_plugin_t *p,
                               const char *var,
                               const char *val)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task = flux_shell_current_task (shell);
    flux_cmd_t *cmd = flux_shell_task_cmd (task);
    if (cmd)
        return flux_cmd_setenvf (cmd, 1, var, "%s", val);
    return 0;
}

static int gpubind_task_init (flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args,
                              void *data)
{
    char *s;
    struct idset *gpus = data;
    struct idset *ids = idset_create (0, IDSET_FLAG_AUTOGROW);

    for (int i = 0; i < ngpus_per_task; i++) {
        unsigned id = idset_first (gpus);
        idset_set (ids, id);
        idset_clear (gpus, id);
    }
    s = idset_encode (ids, IDSET_FLAG_RANGE);
    plugin_task_setenv (p, "CUDA_VISIBLE_DEVICES", s);
    free (s);
    idset_destroy (ids);
    return 0;
}

static int gpubind_init (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    int rc, ngpus, ntasks;
    struct idset *gpus;
    char *opt;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!shell)
        return -1;

    if ((rc = flux_shell_getopt_unpack (shell,
                                       "gpu-affinity",
                                       "s",
                                        &opt)) <= 0) {
        if (rc < 0)
            log_msg ("Failed to get gpu-affinity shell option, ignoring");
        /* gpu-affinity defaults to "on" */
        opt = "on";
    }
    if (strcmp (opt, "off") == 0)
        return 0;
    if (get_shell_gpus (shell, &ntasks, &gpus) < 0)
        return -1;
    if (flux_plugin_aux_set (p, NULL, gpus, (flux_free_f)idset_destroy) < 0) {
        log_err ("flux_plugin_aux_set");
        idset_destroy (gpus);
        return -1;
    }
    if ((ngpus = idset_count (gpus)) <= 0)
        return 0;

    flux_shell_setenvf (shell, 0, "CUDA_DEVICE_ORDER", "PCI_BUS_ID");

    if (strcmp (opt, "per-task") == 0) {
        /*  Set global ngpus_per_task to use in task.init callback:
         */
        ngpus_per_task = ngpus / ntasks;
        if (flux_plugin_add_handler (p,
                                     "task.init",
                                     gpubind_task_init,
                                     gpus) < 0) {
            log_err ("gpubind: flux_plugin_add_handler");
            return -1;
        }
    }
    else {
        char *ids = idset_encode (gpus, IDSET_FLAG_RANGE);
        flux_shell_setenvf (shell, 1, "CUDA_VISIBLE_DEVICES", "%s", ids);
        free (ids);
    }
    return 0;
}

struct shell_builtin builtin_gpubind = {
    .name = "gpu-affinity",
    .init = gpubind_init
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */