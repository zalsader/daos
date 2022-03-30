/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_TEST_DRIVER_H
#define DAOS_DDB_TEST_DRIVER_H

#define assert_uuid_equal(a, b) \
	do {                        \
        char str_a[DAOS_UUID_STR_SIZE];                     \
        char str_b[DAOS_UUID_STR_SIZE];                     \
        uuid_unparse(a, str_a);       \
        uuid_unparse(b, str_b);       \
        assert_string_equal(str_a, str_b);                     \
	} while (0)
#define assert_uuid_not_equal(a, b) \
	do {                        \
        char str_a[DAOS_UUID_STR_SIZE];                     \
        char str_b[DAOS_UUID_STR_SIZE];                     \
        uuid_unparse(a, str_a);       \
        uuid_unparse(b, str_b);       \
        assert_string_not_equal(str_a, str_b);                     \
	} while (0)
#define assert_oid_equal(a, b) \
	do {                       \
        assert_int_equal((a).hi, (b).hi); \
	assert_int_equal((a).lo, (b).lo);                    \
	} while (0)

#define assert_oid_not_equal(a, b) assert_true(a.hi != b.hi || a.lo != b.lo)
#define assert_key_equal(a, b) \
	do {                          \
          assert_int_equal(a.iov_len, b.iov_len);           \
          assert_int_equal(a.iov_buf_len, b.iov_buf_len);          \
          assert_memory_equal(a.iov_buf, b.iov_buf, a.iov_len);                 \
	} while (0)

extern const char 	*g_uuids_str[10];
extern uuid_t 		 g_uuids[10];
extern daos_unit_oid_t 	 g_oids[10];
extern char 		*g_dkeys_str[10];
extern char 		*g_akeys_str[10];
extern daos_key_t 	 g_dkeys[10];
extern daos_key_t 	 g_akeys[10];

struct dv_test_ctx {
	daos_handle_t	dvt_poh;
	uuid_t		dvt_pool_uuid;
	int		dvt_fd;
	char 		dvt_pmem_file[32];
};

daos_unit_oid_t gen_uoid(uint32_t lo);

void dvt_vos_insert_recx(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
			 int recx_idx, char *data_str, daos_epoch_t epoch);
void
dvt_vos_insert_single(daos_handle_t coh, daos_unit_oid_t uoid, char *dkey_str, char *akey_str,
		      char *data_str, daos_epoch_t epoch);

void dvt_iov_alloc(d_iov_t *iov, size_t len);
void dvt_iov_alloc_str(d_iov_t *iov, const char *str);


int ddb_suit_setup(void **state);
int ddb_suit_teardown(void **state);
int ddb_test_setup(void **state);
int ddb_test_teardown(void **state);


int ddb_tests_run();
int dv_tests_run();
int dvc_tests_run();
int ddb_cli_tests();

void dvt_insert_data(daos_handle_t poh);
void dvt_delete_all_containers(daos_handle_t poh);

#endif //DAOS_DDB_TEST_DRIVER_H
