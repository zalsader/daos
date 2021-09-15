/*
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <daos_srv/vos.h>
#include <daos_srv/srv_csum.h>
#include <gurt/telemetry_producer.h>
#include <daos/pool.h>
#include <daos_prop.h>
#include "srv_internal.h"

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)

#define DF_PTGT DF_UUID"[%d]"
#define DP_PTGT(uuid, tgt) DP_UUID(uuid), tgt

/*
 * DAOS_CSUM_SCRUB_DISABLED can be set in the server config to disable the
 * scrubbing ULT completely for the engine.
 */
static inline bool
scrubbing_is_enabled()
{
	char *disabled = getenv("DAOS_CSUM_SCRUB_DISABLED");

	return disabled == NULL;
}

static inline uint32_t
evict_threshold()
{
	char *thresh_str = getenv("DAOS_CSUM_SCRUB_EVICT_THRESH");

	return ((thresh_str != NULL) ? atol(thresh_str) : 10);
}

static inline int
yield_fn(void *arg)
{
	sched_req_yield(arg);

	return 0;
}

static inline int
sleep_fn(void *arg, uint32_t msec)
{
	sched_req_sleep(arg, msec);

	return 0;
}

static inline int
sc_schedule(struct scrub_ctx *ctx)
{
	return ctx->sc_pool->sp_scrub_sched;
}

static int
cont_lookup_cb(uuid_t pool_uuid, uuid_t cont_uuid, void *arg,
	       struct cont_scrub *cont)
{
	struct ds_cont_child	*cont_child = NULL;
	int			 rc;

	rc = ds_cont_child_lookup(pool_uuid, cont_uuid, &cont_child);
	if (rc != 0) {
		D_ERROR("failed to get cont child: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	cont->scs_cont_csummer = cont_child->sc_csummer;
	cont->scs_cont_hdl = cont_child->sc_hdl;
	uuid_copy(cont->scs_cont_uuid, cont_uuid);
	cont->scs_cont_src = cont_child;

	ABT_mutex_lock(cont_child->sc_mutex);
	cont_child->sc_scrubbing = 1;
	ABT_mutex_unlock(cont_child->sc_mutex);

	return 0;
}

static inline void
cont_put_cb(void *cont)
{
	struct ds_cont_child *cont_child = cont;

	ABT_mutex_lock(cont_child->sc_mutex);
	cont_child->sc_scrubbing = 0;
	ABT_cond_broadcast(cont_child->sc_scrub_cond);
	ABT_mutex_unlock(cont_child->sc_mutex);

	ds_cont_child_put(cont_child);
}

static inline bool
cont_is_stopping_cb(void *cont)
{
	struct ds_cont_child *cont_child = cont;

	return cont_child->sc_stopping == 1;
}

static void
sc_add_pool_metrics(struct scrub_ctx *ctx)
{
	d_tm_add_metric(&ctx->sc_metrics.scm_start,
			D_TM_TIMESTAMP,
			"When the current scrubbing started", NULL,
			DF_POOL_DIR"/"M_STARTED, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_end, D_TM_TIMESTAMP, "", "",
			DF_POOL_DIR"/"M_ENDED, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_pool_ult_wait_time, D_TM_GAUGE,
			"How long waiting between checksum calculations", "ms",
			DF_POOL_DIR"/sleep", DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_last_duration,
			D_TM_DURATION,
			"How long the previous scrub took", "ms",
			DF_POOL_DIR"/"M_LAST_DURATION, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_csum_calcs,
			D_TM_COUNTER, "Number of checksums calculated for "
				      "current scan",
			NULL,
			DF_POOL_DIR"/"M_CSUM_COUNTER, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_last_csum_calcs,
			D_TM_COUNTER, "Number of checksums calculated in last "
				      "scan", NULL,
			DF_POOL_DIR"/"M_CSUM_PREV_COUNTER,
			DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_total_csum_calcs,
			D_TM_COUNTER, "Total number of checksums calculated",
			NULL,
			DF_POOL_DIR"/"M_CSUM_TOTAL_COUNTER, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_corruption,
			D_TM_COUNTER, "Number of silent data corruption "
				      "detected during current scan",
			NULL,
			DF_POOL_DIR"/"M_CSUM_CORRUPTION, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_total_corruption,
			D_TM_COUNTER, "Total number of silent data corruption "
				      "detected",
			NULL,
			DF_POOL_DIR"/"M_CSUM_TOTAL_CORRUPTION,
			DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_corrupt_targets, D_TM_COUNTER,
			"Number of corrupt target", "targets",
			"events/corrupt_target");
}


static int
get_crt_group_rank(d_rank_t *rank)
{
	return crt_group_rank(NULL, rank);
}

struct scrub_iv {
	uuid_t		siv_pool_uuid;
	d_rank_t	siv_rank;
	int		siv_target;
};

static int
scrub_iv_alloc_internal(d_sg_list_t *sgl)
{
	int	rc;

	rc = d_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, sizeof(struct scrub_iv));
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	sgl->sg_iovs[0].iov_buf_len = sizeof(struct scrub_iv);

free:
	if (rc)
		d_sgl_fini(sgl, true);
	return rc;
}


static int
scrub_iv_ent_init(struct ds_iv_key *iv_key, void *data,
		    struct ds_iv_entry *entry)
{
	int rc;

	rc = scrub_iv_alloc_internal(&entry->iv_value);
	if (rc)
		return rc;

	entry->iv_key.class_id = iv_key->class_id;
	entry->iv_key.rank = iv_key->rank;
	return 0;
}

static int
scrub_iv_ent_get(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
scrub_iv_ent_put(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
scrub_iv_ent_destroy(d_sg_list_t *sgl)
{
	d_sgl_fini(sgl, true);
	return 0;
}

static int
scrub_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key,
		     d_sg_list_t *dst, void **priv)
{
	D_ASSERT(0);
}

static int
drain_pool_ult(uuid_t pool_uuid, d_rank_t rank, uint32_t target)
{
	int rc;
	d_rank_list_t			 out_ranks = {0};
	struct pool_target_addr_list	 target_list = {0};
	struct pool_target_addr		 addr = {0};

	rc = ds_pool_get_ranks(pool_uuid, MAP_RANKS_UP, &out_ranks);
	if (rc != DER_SUCCESS) {
		D_ERROR("Couldn't get ranks: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	target_list.pta_number = 1;
	addr.pta_rank = rank;
	addr.pta_target = target;
	target_list.pta_addrs = &addr;

	rc = ds_pool_target_update_state(pool_uuid, &out_ranks,
					 &target_list, PO_COMP_ST_DRAIN);
	if (rc != DER_SUCCESS)
		D_ERROR("pool target update status failed: "DF_RC"\n",
			DP_RC(rc));
	map_ranks_fini(&out_ranks);

	return rc;
}

static int
scrub_iv_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
		      d_sg_list_t *src, void **priv)
{
	struct scrub_iv *src_iv = src->sg_iovs[0].iov_buf;
	d_rank_t	  rank;
	int		  rc;

	rc = crt_group_rank(NULL, &rank);
	if (rc)
		return rc;

	return drain_pool_ult(src_iv->siv_pool_uuid, src_iv->siv_rank,
		       src_iv->siv_target);
}

static int
scrub_iv_ent_refresh(struct ds_iv_entry *entry, struct ds_iv_key *key,
		       d_sg_list_t *src, int ref_rc, void **priv)
{
	return 0;
}

static int
scrub_iv_alloc(struct ds_iv_entry *entry, struct ds_iv_key *key,
		 d_sg_list_t *sgl)
{
	return scrub_iv_alloc_internal(sgl);
}


struct ds_iv_class_ops scrub_iv_ops = {
	.ivc_ent_init		= scrub_iv_ent_init,
	.ivc_ent_get		= scrub_iv_ent_get,
	.ivc_ent_put		= scrub_iv_ent_put,
	.ivc_ent_destroy	= scrub_iv_ent_destroy,
	.ivc_ent_fetch		= scrub_iv_ent_fetch,
	.ivc_ent_update		= scrub_iv_ent_update,
	.ivc_ent_refresh	= scrub_iv_ent_refresh,
	.ivc_value_alloc	= scrub_iv_alloc,
};

int
scrub_iv_init(void)
{
	return ds_iv_class_register(IV_CSUM_SCRUBBER, &iv_cache_ops,
				    &scrub_iv_ops);
}

int
scrub_iv_fini(void)
{
	return ds_iv_class_unregister(IV_CSUM_SCRUBBER);
}

/* IV is used to communicate the need for a drain to be initialized */
void
scrub_iv_update_ult(void *arg)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct ds_pool		*pool = arg;
	struct scrub_iv		 iv = {0};
	d_sg_list_t		 sgl;
	d_iov_t			 iov;
	struct ds_iv_key	 key;
	int			 rc;

	uuid_copy(iv.siv_pool_uuid, pool->sp_uuid);
	crt_group_rank(NULL, &iv.siv_rank);
	iv.siv_target = dmi->dmi_tgt_id;

	iov.iov_buf = &iv;
	iov.iov_len = sizeof(iv);
	iov.iov_buf_len = sizeof(iv);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_CSUM_SCRUBBER;
	rc = ds_iv_update(pool->sp_iv_ns, &key, &sgl, CRT_IV_SHORTCUT_TO_ROOT,
			  CRT_IV_SYNC_NONE, 0, false);
	if (rc)
		D_ERROR("iv update failed "DF_RC"\n", DP_RC(rc));
}

static int
drain_pool_tgt_cb(struct ds_pool *pool)
{
	int				rc;
	ABT_thread			thread = ABT_THREAD_NULL;

	/* Create a ULT to update in xstream 0. IV is used to communicate to
	 * leader that a drain needs to be initialized
	 */
	rc = dss_ult_create(scrub_iv_update_ult, pool, DSS_XS_SYS, 0, 0,
			    &thread);

	if (rc != 0) {
		D_ERROR("Error starting ULT: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	ABT_thread_join(thread);
	ABT_thread_free(&thread);

	return 0;

}

/** Setup scrubbing context and start scrubbing the pool */
static void
scrubbing_ult(void *arg)
{
	struct scrub_ctx	 ctx = {0};
	struct ds_pool_child	*child = arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	uuid_t			 pool_uuid;
	daos_handle_t		 poh;
	int			 tgt_id;
	int			 rc;

	poh = child->spc_hdl;
	uuid_copy(pool_uuid, child->spc_uuid);
	tgt_id = dmi->dmi_tgt_id;

	C_TRACE(DF_PTGT": Scrubbing ULT started\n", DP_PTGT(pool_uuid, tgt_id));

	D_ASSERT(child->spc_scrubbing_req != NULL);

	uuid_copy(ctx.sc_pool_uuid, pool_uuid);
	ctx.sc_vos_pool_hdl = poh;
	ctx.sc_sleep_fn = sleep_fn;
	ctx.sc_yield_fn = yield_fn;
	ctx.sc_sched_arg = child->spc_scrubbing_req;
	ctx.sc_pool = child->spc_pool;
	ctx.sc_cont_lookup_fn = cont_lookup_cb;
	ctx.sc_cont_put_fn = cont_put_cb;
	ctx.sc_cont_is_stopping_fn = cont_is_stopping_cb;
	ctx.sc_status = SCRUB_STATUS_NOT_RUNNING;
	ctx.sc_credits_left = ctx.sc_pool->sp_scrub_cred;
	ctx.sc_dmi =  dss_get_module_info();
	ctx.sc_drain_pool_tgt_fn = drain_pool_tgt_cb;
	ctx.sc_get_rank_fn = get_crt_group_rank;
	ctx.sc_pool_evict_threshold = evict_threshold();

	sc_add_pool_metrics(&ctx);
	while (!dss_ult_exiting(child->spc_scrubbing_req)) {
		rc = vos_scrub_pool(&ctx);
		if (rc != 0)
			break;
	}
}

/** Setup and create the scrubbing ult */
int
ds_start_scrubbing_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr = {0};
	ABT_thread		 thread = ABT_THREAD_NULL;
	int			 rc;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_scrubbing_req == NULL);

	/** Don't even create the ULT if scrubbing is disabled. */
	if (!scrubbing_is_enabled()) {
		C_TRACE(DF_PTGT": Checksum scrubbing DISABLED.\n",
			DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id));
		return 0;
	}

	C_TRACE(DF_PTGT": Checksum scrubbing Enabled. Creating ULT.\n",
		DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id));
	rc = scrub_iv_init();
	if (rc != DER_SUCCESS && rc != -DER_EXIST)
		D_ERROR("IV init error: "DF_RC"\n", DP_RC(rc));

	rc = dss_ult_create(scrubbing_ult, child, DSS_XS_SELF, 0, 0,
			    &thread);
	if (rc) {
		D_ERROR(DF_PTGT": Failed to create Scrubbing ULT. "DF_RC"\n",
			DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id), DP_RC(rc));
		return rc;
	}

	D_ASSERT(thread != ABT_THREAD_NULL);

	sched_req_attr_init(&attr, SCHED_REQ_SCRUB, &child->spc_uuid);
	child->spc_scrubbing_req = sched_req_get(&attr, thread);
	if (child->spc_scrubbing_req == NULL) {
		D_CRIT(DF_PTGT": Failed to get req for Scrubbing ULT\n",
		       DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id));
		ABT_thread_join(thread);
		return -DER_NOMEM;
	}

	return 0;
}

void
ds_stop_scrubbing_ult(struct ds_pool_child *child)
{
	struct dss_module_info *dmi = dss_get_module_info();

	D_ASSERT(child != NULL);
	/* Scrubbing ULT is not started */
	if (child->spc_scrubbing_req == NULL)
		return;

	C_TRACE(DF_PTGT": Stopping Scrubbing ULT\n",
		DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id));

	sched_req_wait(child->spc_scrubbing_req, true);
	sched_req_put(child->spc_scrubbing_req);
	child->spc_scrubbing_req = NULL;

	scrub_iv_fini();
}
