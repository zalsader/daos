/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <setjmp.h> /** For cmocka.h */
#include <stdarg.h> /** For cmocka.h */
#include <cmocka.h>
#include <ddb_run_cmds.h>
#include <daos/tests_lib.h>
#include <ddb.h>
#include "ddb_test_driver.h"

/*
 * Test that the command line interface interacts with a 'user' correctly. Will verify that the
 * command line options and arguments are handled correctly and the interactive mode.
 */

static bool g_verbose; /* Can be set to true while developing/debugging tests */

#define test_run_inval_cmd(...) \
	assert_rc_equal(-DER_INVAL, __test_run_cmd((char *[]){"prog_name", __VA_ARGS__, NULL}))
#define test_run_cmd(...) \
	assert_success(__test_run_cmd((char *[]){"prog_name", __VA_ARGS__, NULL}))

static int
__test_run_cmd(char *argv[])
{
	struct argv_parsed	parse_args = {0};
	uint32_t		argc = 0;
	struct ddb_ctx		ctx = {0};

	assert_non_null(argv);
	if (g_verbose)
		printf("Command: ");
	while(argv[argc] != NULL) {
		if (g_verbose)
			printf("%s ", argv[argc]);
		argc++;
	}
	if (g_verbose)
		printf("\n");

	parse_args.ap_argv = argv;
	parse_args.ap_argc = argc;

	/* argv should be {prog_name, cmd_name, ...} */
	return ddb_run_cmd(argv[1], &ctx, &parse_args);
}

#define assert_main(...) \
	assert_success(__test_run_main((char *[]){"prog_name", __VA_ARGS__, NULL}))
#define assert_invalid_main(...) \
	assert_rc_equal(-DER_INVAL, __test_run_main((char *[]){"prog_name", __VA_ARGS__, NULL}))


uint32_t fake_print_message_count;
static int
fake_print_message(const char *fmt, ...)
{
	return 0;
}

uint32_t fake_get_input_called;
int fake_get_input_inputs_count;
int fake_get_input_inputs_idx;
char fake_get_input_inputs[64][64];

#define set_fake_inputs(...) __set_fake_inputs((char *[]){__VA_ARGS__, NULL})
static inline void
__set_fake_inputs(char *inputs[])
{
	int i = 0;

	while (inputs[i] != NULL) {
		/* input from user will always have a new line at the end */
		sprintf(fake_get_input_inputs[i], "%s\n", inputs[i]);
		i++;
	}
	fake_get_input_inputs_count = i;
	fake_get_input_inputs_idx = 0;
}

static char *
fake_get_input (char *buf, uint32_t buf_len)
{
	char *input = fake_get_input_inputs[fake_get_input_inputs_idx++];

	strncpy(buf, input, min(strlen(input) + 1, buf_len));
	fake_get_input_called++;

	return input;
}

static int
__test_run_main(char *argv[])
{
	struct argv_parsed	parse_args = {0};
	uint32_t		argc = 0;
	struct ddb_ctx		ctx = {0};
	struct ddb_io_ft ft = {
		.ddb_get_input = fake_get_input,
		.ddb_print_message = fake_print_message};

	assert_non_null(argv);
	if (g_verbose)
		printf("Command: ");
	while(argv[argc] != NULL && strcmp(argv[argc], "") != 0) {
		if (g_verbose)
			printf("%s ", argv[argc]);
		argc++;
	}
	if (g_verbose)
		printf("\n");

	parse_args.ap_argv = argv;
	parse_args.ap_argc = argc;

	return ddb_main(&ft, argc, argv);
}

#define assert_main_interactive_with_input(...) \
	__assert_main_interactive_with_input((char *[]) {__VA_ARGS__, NULL})
static void
__assert_main_interactive_with_input(char *inputs[])
{
	__set_fake_inputs(inputs);
	assert_main("");
}

int quit_fn_called;
static int
quit_fn(struct ddb_ctx *ctx)
{
	quit_fn_called++;
	ctx->dc_should_quit = true;
	return 0;
}

/*
 * -----------------------------------------------
 * Test Functions
 * -----------------------------------------------
 */

static void
test_main(void **state)
{
	g_ddb_cmds_ft.ddb_ft_quit = quit_fn;

	assert_main_interactive_with_input("quit");
	assert_int_equal(1, fake_get_input_called);
	assert_int_equal(1, quit_fn_called);

	fake_get_input_called = 0;
	assert_main_interactive_with_input("ls", "ls", "quit");
	assert_int_equal(3, fake_get_input_called);

	assert_invalid_main("path", "invalid_extra_arg");
}

/* Not tested yet:
 *   - -R is passed
 *   - -f is passed
 *   - -w is passed
 *   - pool shard file is passed as argument
 */

#define TEST(dsc, test) { dsc, test, NULL, NULL }

static const struct CMUnitTest tests[] = {
	TEST("test main entry point", test_main),
};

int
ddb_cli_tests()
{
	return cmocka_run_group_tests_name("ddb cli tests", tests, NULL, NULL);
}
