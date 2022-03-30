/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DDB_COMMON_H
#define __DAOS_DDB_COMMON_H

#include <daos_types.h>
#include <daos_obj.h>
#include <daos/object.h>
#include <daos/common.h>

#define COMMAND_NAME_MAX 64

#define SUCCESS(rc) ((rc) == DER_SUCCESS)

#define ddb_print(ctx, str) \
	do { if (ctx->dc_fn_print) \
		ctx->dc_fn_print(str); \
	else                            \
		printf(str); } while(0)

#define ddb_printf(ctx, fmt, ...) \
	do { if (ctx->dc_fn_print) \
		ctx->dc_fn_print(fmt, __VA_ARGS__); \
	else                            \
		printf(fmt, __VA_ARGS__); \
	} while(0)

struct ddb_ctx {
	bool dc_should_quit;
	daos_handle_t dc_poh;
	daos_handle_t dc_coh;

	int (*dc_fn_print)(const char *fmt, ...);
	int (*dc_fn_write_file)(const char *path, d_iov_t *contents);
};

struct dv_tree_path {
	uuid_t		vtp_cont;
	daos_unit_oid_t vtp_oid;
	daos_key_t	vtp_dkey;
	daos_key_t	vtp_akey;
	daos_recx_t	vtp_recx;
};

struct dv_tree_path_builder {
	struct dv_tree_path	vtp_path;

	/* A user can pass an index of the path part. These indexes will be used to complete
	 * the path parts.
	 */
	uint32_t 		vtp_cont_idx;
	uint32_t 		vtp_oid_idx;
	uint32_t 		vtp_dkey_idx;
	uint32_t 		vtp_akey_idx;
	uint32_t 		vtp_recx_idx;
};

static inline bool
dv_has_cont(struct dv_tree_path *vtp)
{
	return !uuid_is_null(vtp->vtp_cont);
}

static inline bool
dv_has_obj(struct dv_tree_path *vtp)
{
	return !(vtp->vtp_oid.id_pub.lo == 0 &&
		 vtp->vtp_oid.id_pub.hi == 0);
}

static inline bool
dv_has_dkey(struct dv_tree_path *vtp)
{
	return vtp->vtp_dkey.iov_len > 0;
}
static inline bool
dv_has_akey(struct dv_tree_path *vtp)
{
	return vtp->vtp_akey.iov_len > 0;
}

static inline void
vtp_print(struct ddb_ctx *ctx, struct dv_tree_path *vt_path)
{
	if (dv_has_cont(vt_path))
		ddb_printf(ctx, "/["DF_UUIDF"]", DP_UUID(vt_path->vtp_cont));
	if (dv_has_obj(vt_path))
		ddb_printf(ctx, "/["DF_UOID"]",  DP_UOID(vt_path->vtp_oid));
	if (dv_has_dkey(vt_path))
		ddb_printf(ctx, "/[%s]", (char *)vt_path->vtp_dkey.iov_buf);
	if (dv_has_akey(vt_path))
		ddb_printf(ctx, "/[%s]", (char *)vt_path->vtp_akey.iov_buf);

	if (vt_path->vtp_recx.rx_nr > 0)
		ddb_printf(ctx, "/{%lu-%lu}", vt_path->vtp_recx.rx_idx,
			   vt_path->vtp_recx.rx_idx + vt_path->vtp_recx.rx_nr - 1);
	ddb_print(ctx, "/\n");
}

struct argv_parsed {
	char		**ap_argv;
	void		 *ap_ctx;
	uint32_t	  ap_argc;
};

#endif /** __DAOS_DDB_COMMON_H */
