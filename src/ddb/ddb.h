/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_H
#define DAOS_DDB_H

#include <daos_types.h>

int ddb_init();
void ddb_fini();

struct ddb_io_ft {
	int (*ddb_print_message)(const char *fmt, ...);
	char *(*ddb_get_input) (char *buf, uint32_t buf_len);
};

int ddb_main(struct ddb_io_ft *io_ft, int argc, char *argv[]);

#endif //DAOS_DDB_H
