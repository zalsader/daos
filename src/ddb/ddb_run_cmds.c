/**
* (C) Copyright 2022 Intel Corporation.
*
* SPDX-License-Identifier: BSD-2-Clause-Patent
*/
#include <ctype.h>
#include <getopt.h>
#include <gurt/debug.h>

#include "ddb_run_cmds.h"
#include "ddb_common.h"
#define same(a, b) strcmp(a, b) == 0
#define COMMAND_NAME_LS "ls"
#define COMMAND_NAME_QUIT "quit"

/* Parse command line options for the 'ls' command */
int
ls_option_parse(struct ddb_ctx *ctx, uint32_t argc, char **argv, struct ls_options *cmd_args)
{
	char		*options_short = "r";
	int		 index = 0, opt;
	struct option	 options_long[] = {
	{ "recursive", no_argument, NULL, 'r' },
	{ NULL }
	};

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case 'r':
			cmd_args->recursive = true; 
			break;
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}
	D_ASSERT(argc > optind);
	D_ASSERT(same(argv[optind], COMMAND_NAME_LS));
	optind++;

	if (argc - optind > 0) {
		cmd_args->path = argv[optind];
		optind++;
	} 

	if (argc - optind > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[optind]);
		return -DER_INVAL;
	}

	return 0;
}

int
run_cmd_ls(struct ddb_ctx *ctx, struct argv_parsed *parsed)
{
	int rc;
	struct ls_options opt = {0};

	rc = ls_option_parse(ctx, parsed->ap_argc, parsed->ap_argv, &opt);
	if (rc != 0) {
		ddb_print(ctx, "Error parsing options for ddb");
		return rc;
	}

	if (g_ddb_cmds_ft.ddb_ft_ls) {
		return g_ddb_cmds_ft.ddb_ft_ls(ctx, &opt);
	} else {
		printf("Command 'ddb' not implemented.\n");
		return -DER_INVAL;
	}
}

int
run_cmd_quit(struct ddb_ctx *ctx)
{
	if (g_ddb_cmds_ft.ddb_ft_quit) {
		return g_ddb_cmds_ft.ddb_ft_quit(ctx);
	} else {
		printf("Command 'ddb' not implemented.\n");
		return -DER_INVAL;
	}
}

int
ddb_run_cmd(const char *cmd, struct ddb_ctx *ctx, struct argv_parsed *parsed)
{
	if (same(cmd, COMMAND_NAME_LS)) {
		return run_cmd_ls(ctx, parsed);
	}

	if (same(cmd, COMMAND_NAME_QUIT)) {
		return run_cmd_quit(ctx);
	}

	ddb_printf(ctx, "Invalid command: %s. Valid commands: 'ls', 'quit'", cmd);

	return -DER_INVAL;
}