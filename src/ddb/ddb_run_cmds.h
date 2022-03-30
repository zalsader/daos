/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DDB_RUN_CMDS_H
#define __DDB_RUN_CMDS_H
#include "ddb_common.h"

struct ddb_cmds_ft g_ddb_cmds_ft;

int ddb_run_cmd(const char *cmd, struct ddb_ctx *ctx, struct argv_parsed *parsed);

struct ls_options {
	bool recursive;
	char *path;
};

// Run commands ...
struct ddb_cmds_ft {
        int (*ddb_ft_ls)(struct ddb_ctx *ctx, struct ls_options *opt);
        int (*ddb_ft_quit)(struct ddb_ctx *ctx);
    };

int ddb_run_ls(struct ddb_ctx *ctx, struct ls_options *opt);
int ddb_run_quit(struct ddb_ctx *ctx);

#endif /** __DDB_RUN_CMDS_H */