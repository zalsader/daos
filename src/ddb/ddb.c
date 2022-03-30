/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ddb.h"
#include "ddb_common.h"
#include "ddb_parse.h"
#include "ddb_vos.h"
#include "ddb_run_cmds.h"
#include <daos/common.h>
#include <daos/object.h>

int
ddb_init()
{
	int rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (!SUCCESS(rc))
		return rc;

	rc = obj_class_init();

	g_ddb_cmds_ft.ddb_ft_ls = ddb_run_ls;
	g_ddb_cmds_ft.ddb_ft_quit = ddb_run_quit;

	return rc;
}

void
ddb_fini()
{
	obj_class_fini();
	daos_debug_fini();
}

int
ddb_main(struct ddb_io_ft *io_ft, int argc, char *argv[])
{
	struct program_args	 pa = {0};
	uint32_t		 input_buf_len = 1024;
	uint32_t		 buf_len = input_buf_len * 2;
	char			 buf[buf_len + 1024];
	char			 input_buf[input_buf_len];
	struct argv_parsed	 parse_args = {0};
	int			 rc;
	struct ddb_ctx		 ctx = {0};
	char			*cmd;

	rc = ddb_parse_program_args(argc, argv, &pa);
	if (!SUCCESS(rc))
		return rc;

	if (strlen(pa.pa_pool_path) > 0) {
		rc = ddb_vos_pool_open(&ctx, pa.pa_pool_path);
		if (!SUCCESS(rc))
			return rc;
	}

	if (strlen(pa.pa_r_cmd_run) > 0) {
		/* Add program name back */
		snprintf(buf, buf_len, "%s %s", argv[0], pa.pa_r_cmd_run);
		rc = ddb_str2argv_create(buf, &parse_args);
		if (!SUCCESS(rc)) {
			ddb_vos_pool_close(&ctx);
			return rc;
		}
		cmd = parse_args.ap_argv[1];
		rc = ddb_run_cmd(cmd, &ctx, &parse_args);
		if (!SUCCESS(rc))
			printf("Error with command %s: "DF_RC"\n", cmd, DP_RC(rc));

		ddb_str2argv_free(&parse_args);
		return rc;
	}

	if (strlen(pa.pa_cmd_file) > 0) {
		/* Still to be implemented */

		return -DER_NOSYS;
	}

	while(!ctx.dc_should_quit) {
		io_ft->ddb_print_message("$ ");
		io_ft->ddb_get_input(input_buf, input_buf_len);
		input_buf[strlen(input_buf) - 1] = '\0'; /* Remove newline */

		/* add program name to beginning of string that will be parsed into argv. That way
		 * is the same as argv from command line into main()
		 */
		snprintf(buf, buf_len, "%s %s", argv[0], input_buf);
		rc = ddb_str2argv_create(buf, &parse_args);
		if (!SUCCESS(rc)) {
			io_ft->ddb_print_message("Error with input: "DF_RC"\n", DP_RC(rc));
			ddb_str2argv_free(&parse_args);
			continue;
		}

		if (parse_args.ap_argc > 1)
			ddb_run_cmd(parse_args.ap_argv[1], &ctx, &parse_args);
		else
			io_ft->ddb_print_message("Please enter a valid command\n");
		ddb_str2argv_free(&parse_args);
	}

	ddb_fini();
	return 0;
}