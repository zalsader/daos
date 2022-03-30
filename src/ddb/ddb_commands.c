/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ddb_common.h"
#include <daos/common.h>
#include "ddb_vos.h"
#include "ddb_parse.h"
#include "ddb_run_cmds.h"

int
ddb_run_quit(struct ddb_ctx *ctx)
{
	ctx->dc_should_quit = true;
	return 0;
}

struct ls_ctx {
	struct ddb_ctx	*ctx;
	bool		 has_cont;
	bool		 has_obj;
	bool		 has_dkey;
	bool		 has_akey;
};

static void
print_indent(struct ddb_ctx *ctx, int c)
{
	int i;

	for (i = 0; i < c; i++)
		ddb_print(ctx, "\t");
}

#define DF_IDX "[%d]"
#define DP_IDX(idx) idx

static int
ls_cont_handler(struct ddb_cont *cont, void *args)
{
	struct ls_ctx *ctx = args;

	ctx->has_cont = true;

	ddb_printf(ctx->ctx, DF_IDX" "DF_UUIDF"\n", DP_IDX(cont->ddbc_idx),
		   DP_UUID(cont->ddbc_cont_uuid));

	return 0;
}

static char *
get_object_type(enum daos_otype_t type, char *type_str)
{
	if (type == DAOS_OT_MULTI_HASHED)
		strcpy(type_str, "DAOS_OT_MULTI_HASHED");
	else if (type == DAOS_OT_OIT)
		strcpy(type_str, "DAOS_OT_OIT");
	else if (type == DAOS_OT_DKEY_UINT64)
		strcpy(type_str, "DAOS_OT_DKEY_UINT64");
	else if (type == DAOS_OT_AKEY_UINT64)
		strcpy(type_str, "DAOS_OT_AKEY_UINT64");
	else if (type == DAOS_OT_MULTI_UINT64)
		strcpy(type_str, "DAOS_OT_MULTI_UINT64");
	else if (type == DAOS_OT_DKEY_LEXICAL)
		strcpy(type_str, "DAOS_OT_DKEY_LEXICAL");
	else if (type == DAOS_OT_AKEY_LEXICAL)
		strcpy(type_str, "DAOS_OT_AKEY_LEXICAL");
	else if (type == DAOS_OT_MULTI_LEXICAL)
		strcpy(type_str, "DAOS_OT_MULTI_LEXICAL");
	else if (type == DAOS_OT_KV_HASHED)
		strcpy(type_str, "DAOS_OT_KV_HASHED");
	else if (type == DAOS_OT_KV_UINT64)
		strcpy(type_str, "DAOS_OT_KV_UINT64");
	else if (type == DAOS_OT_KV_LEXICAL)
		strcpy(type_str, "DAOS_OT_KV_LEXICAL");
	else if (type == DAOS_OT_ARRAY)
		strcpy(type_str, "DAOS_OT_ARRAY");
	else if (type == DAOS_OT_ARRAY_ATTR)
		strcpy(type_str, "DAOS_OT_ARRAY_ATTR");
	else if (type == DAOS_OT_ARRAY_BYTE)
		strcpy(type_str, "DAOS_OT_ARRAY_BYTE");
	else
		strcpy(type_str, "UNKNOWN");
}

static int
ls_obj_handler(struct ddb_obj *obj, void *args)
{
	struct ls_ctx	*ctx = args;
	char		 obj_class_name[32];
	daos_oclass_id_t oclass;

	ctx->has_obj = true;

	oclass = daos_obj_id2class(obj->ddbo_oid);
	enum daos_otype_t otype = daos_obj_id2type(obj->ddbo_oid);
	daos_oclass_id2name(oclass, obj_class_name);

	daos_obj_id_t oid = obj->ddbo_oid;
	uint32_t nr_grps;

	nr_grps = (oid.hi & OID_FMT_META_MASK) >> OID_FMT_META_SHIFT;

	char otype_str[32] = {0};
	get_object_type(otype, otype_str);

	print_indent(ctx->ctx, ctx->has_cont);
	ddb_printf(ctx->ctx, DF_IDX" class: %s, type: %s, groups: %d ("DF_OID")\n",
	       DP_IDX(obj->ddbo_idx),
	       obj_class_name,
	       otype_str,
	       nr_grps,
	       DP_OID(obj->ddbo_oid));

	return 0;
}

static int
ls_dkey_handler(struct ddb_key *key, void *args)
{
	struct ls_ctx *ctx = args;

	ctx->has_dkey = true;
	print_indent(ctx->ctx, ctx->has_cont + ctx->has_obj);
	ddb_printf(ctx->ctx, DF_IDX" "DF_KEY"\n", DP_IDX(key->ddbk_idx), DP_KEY(&key->ddbk_key));
	return 0;
}

static int
ls_akey_handler(struct ddb_key *key, void *args)
{
	struct ls_ctx *ctx = args;

	ctx->has_akey = true;
	print_indent(ctx->ctx, ctx->has_cont + ctx->has_obj + ctx->has_dkey);
	ddb_printf(ctx->ctx, DF_IDX" "DF_KEY"\n", DP_IDX(key->ddbk_idx), DP_KEY(&key->ddbk_key));

	return 0;
}

static int
ls_sv_handler(struct ddb_sv *val, void *args)
{
	struct ls_ctx *ctx = args;

	print_indent(ctx->ctx, ctx->has_cont + ctx->has_obj + ctx->has_dkey + ctx->has_akey);
	ddb_printf(ctx->ctx, "Single Value: %lu record size\n", val->ddbs_record_size);
	return 0;
}

static int
ls_array_handler(struct ddb_array *val, void *args)
{
	struct ls_ctx *ctx = args;

	print_indent(ctx->ctx, ctx->has_cont + ctx->has_obj + ctx->has_dkey + ctx->has_akey);
	ddb_printf(ctx->ctx, "[Array] recx: "DF_RECX",  record size: %lu\n",
		   DP_RECX(val->ddba_recx),
		   val->ddba_record_size);

	return 0;
}

static struct vos_tree_handlers handlers = {
	.ddb_cont_handler = ls_cont_handler,
	.ddb_obj_handler = ls_obj_handler,
	.ddb_dkey_handler = ls_dkey_handler,
	.ddb_akey_handler = ls_akey_handler,
	.ddb_array_handler = ls_array_handler,
	.ddb_sv_handler = ls_sv_handler,
};

int
ddb_run_ls(struct ddb_ctx *ctx, struct ls_options *opt)
{
	int rc;
	struct dv_tree_path_builder vt_path = {0};
	struct ls_ctx lsctx = {0};

	rc = ddb_parse_vos_tree_path(opt->path, &vt_path);
	if (!SUCCESS(rc))
		return rc;

	rc = dv_path_update_from_indexes(ctx, &vt_path);
	if (!SUCCESS(rc))
		return rc;

	vtp_print(ctx, &vt_path.vtp_path);
	lsctx.ctx = ctx;
	rc = dv_iterate_path(ctx->dc_poh, &vt_path.vtp_path, opt->recursive, &handlers, &lsctx);

	return rc;

}
