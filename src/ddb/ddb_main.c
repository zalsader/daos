/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include "ddb.h"
#include <daos_types.h>

static char *
get_input(char *buf, uint32_t buf_len)
{
	return fgets(buf, buf_len, stdin);
}

int main(int argc, char *argv[])
{
	int rc;
	struct ddb_io_ft ft = {.ddb_get_input = get_input, .ddb_print_message = printf};

	rc = ddb_init();
	if (rc != 0) {
		fprintf(stderr, "Error with ddb_init: "DF_RC"\n", DP_RC(rc));
		return -rc;
	}
	rc = ddb_main(&ft, argc, argv);
	if (rc != 0)
		fprintf(stderr, "Error: "DF_RC"\n", DP_RC(rc));

	ddb_fini();

	return -rc;
}