/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdarg.h> /** For cmocka.h */
#include <setjmp.h> /** For cmocka.h */
#include <cmocka.h>
#include <gurt/debug.h>
#include <daos/tests_lib.h>
#include <ddb_common.h>
#include <ddb_run_cmds.h>
#include "ddb_test_driver.h"

/*
 * Test that the command line arguments execute the correct tool command with the correct
 * options/arguments for the command. Verification depends on the ability to set fake command
 * functions in a command function table that the program uses.
 */

static bool g_verbose; /* Can be set to true while developing/debugging tests */

#define test_run_inval_cmd(ctx, ...) \
	assert_rc_equal(-DER_INVAL, __test_run_cmd(ctx, (char *[]){"prog_name", __VA_ARGS__, NULL}))
#define test_run_cmd(ctx, ...) \
	assert_success(__test_run_cmd(ctx, (char *[]){"prog_name", __VA_ARGS__, NULL}))

static int
__test_run_cmd(struct ddb_ctx *ctx, char *argv[])
{
	struct argv_parsed	parse_args = {0};
	uint32_t		argc = 0;

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
	return ddb_run_cmd(argv[1], ctx, &parse_args);

}

static uint32_t fake_print_called;
static char fake_print_buffer[1024];
int fake_print(const char *fmt, ...)
{
	va_list args;
	fake_print_called++;
	va_start(args, fmt);
	vsnprintf(fake_print_buffer, ARRAY_SIZE(fake_print_buffer), fmt, args);
	va_end(args);
	if (g_verbose) {
		printf("%s", fake_print_buffer);
	}

	return 0;
}

static int fake_ft_ls_called;
static struct ls_options fake_ft_ls_options;
static int
fake_ft_ls(struct ddb_ctx *ctx, struct ls_options *opt)
{
	fake_ft_ls_options = *opt;
	fake_ft_ls_called++;

	return 0;
}

/*
 * -----------------------------------------------
 * Test Functions
 * -----------------------------------------------
 */
static void
test_ls(void **state)
{
	struct dv_test_ctx	*tctx = *state;
	struct ddb_ctx		 ctx = {0};

	g_ddb_cmds_ft.ddb_ft_ls = fake_ft_ls;
	ctx.dc_poh = tctx->dvt_poh;
	ctx.dc_fn_print = fake_print;

	test_run_inval_cmd(&ctx, "ls path invalid_argument"); /* invalid argument */
	test_run_inval_cmd(&ctx, "ls -z"); /* invalid option */
	test_run_inval_cmd(&ctx, "ls -r recursive"); /* invalid option argument*/

	test_run_cmd(&ctx, "ls");
	assert_int_equal(1, fake_ft_ls_called);
	assert_null(fake_ft_ls_options.path);
	assert_false(fake_ft_ls_options.recursive);

	test_run_cmd(&ctx, "ls", "-r");
	assert_int_equal(2, fake_ft_ls_called);
	assert_true(fake_ft_ls_options.recursive);

	test_run_cmd(&ctx, "ls", "/[0]/[0]");
	assert_string_equal("/[0]/[0]", fake_ft_ls_options.path);
	assert_false(fake_ft_ls_options.recursive);
}

/*
 * --------------------------------------------------------------
 * End test functions
 * --------------------------------------------------------------
 */

static int
dcv_suit_setup(void **state)
{
	struct dv_test_ctx *tctx;

	ddb_suit_setup(state);
	tctx = *state;

	dvt_insert_data(tctx->dvt_poh);
	fake_ft_ls_called = 0;

	return 0;
}

static int
dcv_suit_teardown(void **state)
{
	struct dv_test_ctx *tctx = *state;

	dvt_delete_all_containers(tctx->dvt_poh);
	ddb_suit_teardown(state);

	return 0;
}

#define TEST(dsc, test) { dsc, test, ddb_test_setup, ddb_test_teardown }
static const struct CMUnitTest tests[] = {
	TEST("01: ls ", test_ls),
};

int
dvc_tests_run()
{
	print_message("Running ddb commands tests\n");
	return cmocka_run_group_tests_name("ddb tests", tests, dcv_suit_setup, dcv_suit_teardown);
}