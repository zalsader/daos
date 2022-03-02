#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <daos/common.h>
#include <daos_srv/vos.h>
#include <daos_test.h>
#include <pmfs/pmfs.h>
#include <pmfs/vos_target_fs.h>
#include <pmfs/vos_tasks.h>
#include "perf_internal.h"

static struct vos_fs_cmd_args *g_vfca;
static int count;
D_LIST_HEAD(g_test_pool_list);
D_LIST_HEAD(g_test_fini_list);
enum {
	MKFS,
	MOUNT
};

struct mkfs_args {
	daos_handle_t poh;
	uuid_t uuid;
};

struct mount_args {
	daos_handle_t poh;
	daos_handle_t coh;
	int flags;
	struct pmfs **pmfs;
};

struct umount_args {
	struct pmfs *pmfs;
};

struct mkdir_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *name;
	mode_t mode;
};

struct listdir_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	uint32_t nr;
};

struct remove_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *name;
	bool force;
	daos_obj_id_t *oid;
};

struct open_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *name;
	mode_t mode;
	int flags;
	daos_size_t chunk_size;
	const char *value;
	struct pmfs_obj *obj;
};

struct readdir_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	uint32_t *nr;
	struct dirent *dirs;
};

struct lookup_args {
	struct pmfs *pmfs;
	const char *path;
	int flags;
	struct pmfs_obj *obj;
	mode_t *mode;
	struct stat *stbuf;
};

struct release_args {
	struct pmfs_obj *obj;
};

struct punch_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	daos_off_t offset;
	daos_size_t len;
};

struct write_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	d_sg_list_t *user_sgl;
	daos_off_t off;
};

struct read_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	d_sg_list_t *user_sgl;
	daos_off_t off;
	daos_size_t *read_size;
};

struct stat_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *name;
	struct stat *stbuf;
};

static void
pmfs_buffer_render(char *buf, unsigned int buf_len)
{
	int	nr = 'z' - 'a' + 1;
	int	i;

	for (i = 0; i < buf_len - 1; i++) {
		int randv = rand() % (2 * nr);

		if (randv < nr)
			buf[i] = 'a' + randv;
		else
			buf[i] = 'A' + (randv - nr);
	}
	buf[i] = '\0';
}

static  struct pmfs_pool
pmfs_add_single_pool(char *tsc_pmem_file, uint64_t tsc_nvme_size,
		uint64_t tsc_scm_size, bool tsc_skip_cont_create, bool amend)
{
	struct pmfs_pool *pmfs_pool;

	D_ALLOC(pmfs_pool, sizeof(struct pmfs_pool));
	D_ASSERT(pmfs_pool != NULL);

	if (tsc_pmem_file == NULL) {
		char ts_pmem_file[PATH_MAX];

		snprintf(ts_pmem_file, sizeof(ts_pmem_file),
			 "/mnt/daos/pmfs_cli%d.pmem", count);
		tsc_pmem_file = ts_pmem_file;
		D_PRINT("tsc pmem file = %s\r\n", tsc_pmem_file);
	}
	uuid_generate(pmfs_pool->tsc_pool_uuid);
	pmfs_pool->tsc_pmem_file = tsc_pmem_file;
	pmfs_pool->tsc_nvme_size = tsc_nvme_size;
	pmfs_pool->tsc_scm_size = tsc_scm_size;
	pmfs_pool->tsc_skip_cont_create = tsc_skip_cont_create;
	count++;
	d_list_add(&pmfs_pool->pl, &g_test_pool_list);
	if (amend)
		engine_pool_single_node_init(pmfs_pool, false);

	return *pmfs_pool;
}

static struct pmfs_context
pmfs_set_ctx(void)
{
	struct pmfs_context *pmfs_ctx;

	D_ALLOC(pmfs_ctx, sizeof(struct pmfs_context));
	D_ASSERT(pmfs_ctx != NULL);

	D_INIT_LIST_HEAD(&pmfs_ctx->pmfs_pool.pl);
	/* add pool mapping to /mnt/daos/pmfs_cli0.pmem, 8G NVME, 2G SCM, skip container create */
	pmfs_ctx->pmfs_pool = pmfs_add_single_pool("/mnt/daos/pmfs_cli0.pmem", 8ULL << 30,
			2ULL << 30, true, false);
	/* That aims to associated with engine lib */
	pmfs_ctx->pmfs_pool.pl = g_test_pool_list;
	pmfs_ctx_combine_pool_list(pmfs_ctx);
	return *pmfs_ctx;
}

static void
pmfs_mkfs_cb(void *arg)
{
	struct mkfs_args *mags = arg;

	pmfs_mkfs(mags->poh, mags->uuid);
}

static void
pmfs_mount_cb(void *arg)
{
	struct mount_args *mount_args = arg;

	pmfs_mount(mount_args->poh, mount_args->coh, mount_args->flags,
		   mount_args->pmfs);
}

static void
pmfs_umount_cb(void *arg)
{
	struct umount_args *umount_args = arg;

	pmfs_umount(umount_args->pmfs);
}

static void
pmfs_mkdir_cb(void *arg)
{
	struct mkdir_args *mkdir_args = arg;

	pmfs_mkdir(mkdir_args->pmfs, mkdir_args->parent, mkdir_args->name,
		   mkdir_args->mode);
}

static void
pmfs_listdir_cb(void *arg)
{
	struct listdir_args *listdir_args = arg;

	pmfs_listdir(listdir_args->pmfs, listdir_args->obj, &listdir_args->nr);
}

static void
pmfs_remove_cb(void *arg)
{
	struct remove_args *remove_args = arg;

	pmfs_remove(remove_args->pmfs, remove_args->parent, remove_args->name,
			remove_args->force, remove_args->oid);
}

static void
pmfs_open_cb(void *arg)
{
	struct open_args *open_args = arg;

	pmfs_open(open_args->pmfs, open_args->parent, open_args->name,
		  open_args->mode, open_args->flags, open_args->chunk_size,
		  open_args->value, &open_args->obj);
}

static void
pmfs_readdir_cb(void *arg)
{
	struct readdir_args *readdir_args = arg;

	pmfs_readdir(readdir_args->pmfs, readdir_args->obj, readdir_args->nr,
		     readdir_args->dirs);
}

static void
pmfs_lookup_cb(void *arg)
{
	struct lookup_args *lookup_args = arg;

	pmfs_lookup(lookup_args->pmfs, lookup_args->path, lookup_args->flags,
		    &lookup_args->obj, lookup_args->mode, lookup_args->stbuf);
}

static void
pmfs_release_cb(void *arg)
{
	struct release_args *release_args = arg;

	pmfs_release(release_args->obj);
}

static void
pmfs_punch_cb(void *arg)
{
	struct punch_args *punch_args = arg;

	pmfs_punch(punch_args->pmfs, punch_args->obj, punch_args->offset,
			punch_args->len);
}

static int
pmfs_write_cb(void *arg)
{
	struct write_args *write_args = arg;
	char		*buf;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	int	rc = 0;
	daos_size_t             file_size = 256000;
	daos_size_t     buf_size = 128 * 1024;
	daos_size_t     io_size;
	daos_size_t     size = 0;

	D_ALLOC(buf, buf_size);
	if (buf == NULL) {
		return -DER_NOMEM;
	}
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;


	while (size < file_size) {
		io_size = file_size - size;
		io_size = min(io_size, buf_size);

		sgl.sg_iovs[0].iov_len = io_size;
		pmfs_buffer_render(buf, io_size);
		rc = pmfs_write_sync(write_args->pmfs, write_args->obj, &sgl, size);
		if (rc != 0) {
			D_PRINT("write error\r\n");
			return rc;
		}
		size += io_size;
	}
	D_FREE(buf);
	return 0;
}

static int
pmfs_read_cb(void *arg)
{
	struct read_args *read_args = arg;

	char *buf;
	d_sg_list_t sgl;
	d_iov_t iov;
	daos_size_t buf_size;
	daos_size_t read_size, got_size;
	daos_size_t stride_size = 1024*16;
	daos_size_t total_size = 256000;
	daos_size_t off = read_args->off;
	int rc = 0;

	buf_size = stride_size;
	D_ALLOC(buf, buf_size);
	D_ASSERT(buf != NULL);
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;

	while (off < total_size) {
		read_size = min(total_size - off, stride_size);
		sgl.sg_iovs[0].iov_len = read_size;
		rc = pmfs_read_sync(read_args->pmfs, read_args->obj, &sgl, off,
				&got_size);
		if (rc != 0 || read_size != got_size) {
			if (rc != 0)
				D_PRINT("read error!\r\n");
			else
				D_PRINT("verify size error!\r\n");
			return rc;
		}
		off += stride_size;
	}

	D_FREE(buf);
	return rc;
}

static void
pmfs_stat_cb(void *arg)
{
	struct stat_args *stat_args = arg;

	pmfs_stat(stat_args->pmfs, stat_args->parent, stat_args->name,
			stat_args->stbuf);
}

static int
pmfs_mount_start(daos_handle_t poh, daos_handle_t coh, struct pmfs **pmfs)
{
	struct mount_args mount_args;
	static pthread_t mount_thread;

	int rc = 0;

	memset(&mount_args, 0, sizeof(mount_args));
	mount_args.poh = poh;
	mount_args.coh = coh;
	mount_args.flags = O_RDWR;
	mount_args.pmfs = pmfs;
	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("----start pmfs mount---------------------------\r\n");
	rc = pthread_create(&mount_thread, NULL, (void *) pmfs_mount_cb,
			     &mount_args);

	if (rc != 0) {
		D_PRINT("pmfs mount cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_join(mount_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_mkdir_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		 mode_t mode)
{
	struct mkdir_args mkdir_args;
	static pthread_t mkdir_thread;
	int rc = 0;

	memset(&mkdir_args, 0, sizeof(mkdir_args));
	mkdir_args.pmfs = pmfs;
	mkdir_args.parent = parent;
	mkdir_args.name = name;
	mkdir_args.mode = mode;
	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("----start pmfs mkdir---------------------------\r\n");
	rc = pthread_create(&mkdir_thread, NULL, (void *) pmfs_mkdir_cb,
			     &mkdir_args);

	if (rc != 0) {
		D_PRINT("pmfs mkdir cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_join(mkdir_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_listdir_start(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr)
{
	struct listdir_args listdir_args;
	static pthread_t listdir_thread;
	int rc = 0;

	memset(&listdir_args, 0, sizeof(listdir_args));
	listdir_args.pmfs = pmfs;
	listdir_args.obj = obj;
	listdir_args.nr = *nr;

	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("----start pmfs listdir---------------------------\r\n");
	rc = pthread_create(&listdir_thread, NULL, (void *) pmfs_listdir_cb,
			     &listdir_args);

	if (rc != 0) {
		D_PRINT("pmfs listdir cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_join(listdir_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	*nr = listdir_args.nr;

	return rc;
}

static int
pmfs_remove_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		bool force, daos_obj_id_t *oid)
{
	struct remove_args remove_args;
	static pthread_t remove_thread;
	int rc = 0;

	memset(&remove_args, 0, sizeof(remove_args));
	remove_args.pmfs = pmfs;
	remove_args.parent = parent;
	remove_args.name = name;
	remove_args.force = force;
	remove_args.oid = oid;

	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("----start pmfs remove ---------------------------\r\n");
	rc = pthread_create(&remove_thread, NULL, (void *) pmfs_remove_cb,
			     &remove_args);

	if (rc != 0) {
		D_PRINT("pmfs remove cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_join(remove_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_open_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		mode_t mode, int flags, daos_size_t chunk_size,
		const char *value, struct pmfs_obj **_obj)
{
	struct open_args open_args;
	static pthread_t open_thread;
	int rc = 0;

	memset(&open_args, 0, sizeof(open_args));
	open_args.pmfs = pmfs;
	open_args.parent = parent;
	open_args.name = name;
	open_args.mode = mode;
	open_args.flags = flags;
	open_args.chunk_size = chunk_size;
	open_args.value = value;
	open_args.obj = *_obj;


	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("----start pmfs open obj------------------------\r\n");
	rc = pthread_create(&open_thread, NULL, (void *) pmfs_open_cb,
			     &open_args);

	if (rc != 0) {
		D_PRINT("pmfs open obj cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_join(open_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	*_obj = open_args.obj;

	return rc;
}

static int
pmfs_readdir_start(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr,
		   struct dirent *dirs)
{
	struct readdir_args readdir_args;
	static pthread_t readdir_thread;
	int rc = 0;

	memset(&readdir_args, 0, sizeof(readdir_args));
	readdir_args.pmfs = pmfs;
	readdir_args.obj = obj;
	readdir_args.nr = nr;
	readdir_args.dirs = dirs;

	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("----start pmfs readdir------------------------\r\n");
	rc = pthread_create(&readdir_thread, NULL, (void *) pmfs_readdir_cb,
			     &readdir_args);

	if (rc != 0) {
		D_PRINT("pmfs readdir cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_join(readdir_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_lookup_start(struct pmfs *pmfs, const char *path, int flags,
		struct pmfs_obj **obj, mode_t *mode, struct stat *stbuf)
{
	struct lookup_args lookup_args;
	static pthread_t lookup_thread;
	int rc = 0;

	memset(&lookup_args, 0, sizeof(lookup_args));
	lookup_args.pmfs = pmfs;
	lookup_args.path = path;
	lookup_args.flags = flags;
	lookup_args.obj = *obj;
	lookup_args.mode = mode;
	lookup_args.stbuf = stbuf;

	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("---------------start pmfs lookup------------------------\r\n");
	rc = pthread_create(&lookup_thread, NULL, (void *) pmfs_lookup_cb,
			     &lookup_args);

	if (rc != 0) {
		D_PRINT("pmfs lookup cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_join(lookup_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	*obj = lookup_args.obj;

	return rc;
}

static int
pmfs_punch_start(struct pmfs *pmfs, struct pmfs_obj *obj, daos_off_t offset,
		daos_size_t len)
{
	struct punch_args punch_args;
	static pthread_t punch_thread;
	int rc = 0;

	memset(&punch_args, 0, sizeof(punch_args));
	punch_args.pmfs = pmfs;
	punch_args.obj = obj;
	punch_args.offset = offset;
	punch_args.len = len;

	g_vfca->vfcmd = "PMFS_TASKS";
	D_PRINT("----start pmfs punch file obj offset=%ld, len=%ld--\r\n", offset, len);

	rc = pthread_create(&punch_thread, NULL, (void *)pmfs_punch_cb, &punch_args);
	if (rc != 0) {
		D_PRINT("punch file failed\r\n");
		return rc;
	}

	rc = pthread_join(punch_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_write_start(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *user_sgl,
		daos_off_t off)
{
	struct write_args write_args;
	static pthread_t write_thread;
	int rc = 0;

	memset(&write_args, 0, sizeof(write_args));
	write_args.pmfs = pmfs;
	write_args.obj = obj;
	write_args.user_sgl = user_sgl;
	write_args.off = off;

	g_vfca->vfcmd = "PMFS_TASKS";
	D_PRINT("----start pmfs write file obj offset=%ld\r\n", off);

	rc = pthread_create(&write_thread, NULL, (void *)pmfs_write_cb, &write_args);
	if (rc != 0) {
		D_PRINT("write file failed\r\n");
		return rc;
	}

	rc = pthread_join(write_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_read_start(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *user_sgl,
		daos_off_t off, daos_size_t *read_size)
{
	struct read_args read_args;
	static pthread_t read_thread;
	int rc = 0;

	memset(&read_args, 0, sizeof(read_args));
	read_args.pmfs = pmfs;
	read_args.obj = obj;
	read_args.user_sgl = user_sgl;
	read_args.off = off;
	read_args.read_size = read_size;

	g_vfca->vfcmd = "PMFS_TASKS";
	D_PRINT("----start pmfs read file obj -------------------\r\n");

	rc = pthread_create(&read_thread, NULL, (void *)pmfs_read_cb, &read_args);
	if (rc != 0) {
		D_PRINT("punch file failed\r\n");
		return rc;
	}

	rc = pthread_join(read_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_stat_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		struct stat *stbuf)
{
	struct stat_args stat_args;
	static pthread_t stat_thread;
	int rc = 0;

	memset(&stat_args, 0, sizeof(stat_args));
	stat_args.pmfs = pmfs;
	stat_args.parent = parent;
	stat_args.name = name;
	stat_args.stbuf = stbuf;

	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("---------------start pmfs stat start -------------------\r\n");

	rc = pthread_create(&stat_thread, NULL, (void *)pmfs_stat_cb, &stat_args);
	if (rc != 0) {
		D_PRINT("pmfs stat failed\r\n");
		return rc;
	}

	rc = pthread_join(stat_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_release_start(struct pmfs_obj *obj)
{
	struct release_args release_args;
	static pthread_t release_thread;
	int rc = 0;

	memset(&release_args, 0, sizeof(release_args));
	release_args.obj = obj;

	g_vfca->vfcmd = "PMFS_TASKS";

	D_PRINT("---------------start pmfs release obj------------------------\r\n");
	rc = pthread_create(&release_thread, NULL, (void *) pmfs_release_cb,
			     &release_args);

	if (rc != 0) {
		D_PRINT("pmfs release obj cmd process thread failed: %d\n",
			  rc);
		return rc;
	}

	rc = pthread_join(release_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}


static int
pmfs_umount_start(struct pmfs *pmfs)
{
	struct umount_args umount_args;
	static pthread_t umount_thread;
	int rc = 0;

	memset(&umount_args, 0, sizeof(umount_args));
	umount_args.pmfs = pmfs;

	D_PRINT("---------------start pmfs umount---------------------------\r\n");
	rc = pthread_create(&umount_thread, NULL, (void *) pmfs_umount_cb,
			     &umount_args);

	if (rc != 0) {
		D_PRINT("pmfs umount cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_join(umount_thread, NULL);
	if (rc != 0) {
		return rc;
	}

	return rc;
}

static int
pmfs_init_pool(void *arg, struct scan_context ctx)
{
	struct vos_fs_cmd_args *vfca = arg;
	int rc;

	uuid_copy(ctx.pool_uuid, vfca->pmfs_ctx.pmfs_pool.tsc_pool_uuid);
	ctx.pool_hdl = vfca->pmfs_ctx.pmfs_pool.tsc_poh;
	ctx.cur_cont = vfca->pmfs_ctx.pmfs_pool.pmfs_container;
	ctx.cur_cont.cl = vfca->pmfs_ctx.pmfs_pool.pmfs_container.cl;
	rc = pmfs_scan_pool(&ctx);

	if (rc != 0) {
		D_PRINT("init pool, rebuild container list failed\r\n");
	}

	vfca->pmfs_ctx.pmfs_pool.pmfs_container = ctx.cur_cont;

	return rc;
}

static int
app_send_thread_cmds_in_pool(void)
{
	struct mkfs_args mags;
	struct pmfs *pmfs;
	static pthread_t cthread;
	static pthread_t pthread;
	struct scan_context ctx = { };
	struct pmfs_pool *pmfs_pool;
	daos_handle_t test_coh;
	int rc;

	memset(&mags, 0, sizeof(mags));
	pmfs_pool = pmfs_find_pool("/mnt/daos/pmfs_cli0.pmem");
	mags.poh = pmfs_pool->tsc_poh;
	uuid_generate(mags.uuid);

	pthread_mutex_init(&g_vfca->g_pro_lock, NULL);

	/* Create cmd thread */

	D_PRINT("---------------start pmfs mkfs---------------------------\r\n");
	rc = pthread_create(&cthread, NULL, (void *) pmfs_mkfs_cb, &mags);

	if (rc != 0) {
		D_PRINT("create fs cmd process thread failed: %d\n", rc);
		return rc;
	}

	rc = pthread_create(&pthread, NULL, (void *) vos_task_process,
			     g_vfca);

	if (rc != 0) {
		D_PRINT("create main process thread failed: %d\n", rc);
		return rc;
	}


	rc = pthread_join(cthread, NULL);
	if (rc != 0) {
		return rc;
	}

	g_vfca->pid = pthread;

	/* start pmfs_init_pool */
	g_vfca->pmfs_ctx.pmfs_pool = *pmfs_pool;
	D_PRINT("---------------start scan pool---------------------------\r\n");
	D_PRINT("---------------rebuild container list before mount-------\r\n");
	rc = pmfs_init_pool(g_vfca, ctx);
	if (rc != 0) {
		return rc;
	}

	D_PRINT("---------------rebuild container list done---------------\r\n");
	/* start mount thread */
	test_coh = g_vfca->pmfs_ctx.pmfs_pool.pmfs_container.tsc_coh;
	rc = pmfs_mount_start(g_vfca->pmfs_ctx.pmfs_pool.tsc_poh, test_coh,
			      &pmfs);
	if (rc != 0) {
		D_PRINT("pmfs mount start failed\r\n");
		return rc;
	}
	D_PRINT("---------------pmfs mount done---------------\r\n");
	/* start mkdir thread */

	rc = pmfs_mkdir_start(pmfs, NULL, "pmfs", O_RDWR);
	if (rc != 0) {
		D_PRINT("pmfs mkdir start failed\r\n");
		return rc;
	}

	rc = pmfs_mkdir_start(pmfs, NULL, "dfs", O_RDWR);
	if (rc != 0) {
		D_PRINT("pmfs mkdir start failed\r\n");
		return rc;
	}
	D_PRINT("---------------pmfs mkdir done---------------\r\n");
	/* start listdir thread */
	uint32_t nr;

	rc = pmfs_listdir_start(pmfs, NULL, &nr);
	if (rc != 0) {
		D_PRINT("pmfs listdir start failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs list %d directories done---\r\n", nr);
	D_PRINT("---------------pmfs listdir done---------------\r\n");
	/* start open obj start */
	struct pmfs_obj *obj = NULL;

	rc = pmfs_open_start(pmfs, NULL, "pmfs", S_IFDIR, O_RDWR | O_CREAT,
			     1024, "sssss", &obj);
	if (rc != 0) {
		D_PRINT("pmfs open start failed\r\n");
		return rc;
	}
	rc = pmfs_mkdir_start(pmfs, obj, "nfs", O_RDWR);
	if (rc != 0) {
		D_PRINT("pmfs mkdir nfs in pmfs start failed\r\n");
		return rc;
	}
	rc = pmfs_mkdir_start(pmfs, obj, "tfs", O_RDWR);
	if (rc != 0) {
		D_PRINT("pmfs mkdir tfs in pmfs start failed\r\n");
		return rc;
	}
	D_PRINT("---------------pmfs open folder pmfs done---------------\r\n");
	/* start readdir obj start */
	struct dirent tmp_dirs = { 0 };

	rc = pmfs_readdir_start(pmfs, obj, &nr, &tmp_dirs);

	D_PRINT("---------------pmfs readdir %s--------------\r\n",
		tmp_dirs.d_name);
	D_PRINT("---------------pmfs readdir done---------------\r\n");
	/* start lookup start */
	struct pmfs_obj *tmp_obj = NULL;
	mode_t mode;

	rc = pmfs_lookup_start(pmfs, "/pmfs", 1, &tmp_obj, &mode, NULL);
	if (rc != 0) {
		D_PRINT("pmfs lookup start failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs lookup done---------------\r\n");
	/* start pmfs remove */
	daos_obj_id_t oid;

	rc = pmfs_remove_start(pmfs, obj, "tfs", true, &oid);
	if (rc != 0) {
		D_PRINT("pmfs remove start failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs remove done---------------\r\n");
	/* start pmfs stat */
	struct stat stbuf;

	rc = pmfs_stat_start(pmfs, obj, "nfs", &stbuf);
	if (rc != 0) {
		D_PRINT("pmfs stat start failed\r\n");
		return rc;
	}
	D_PRINT("total size =%ld", stbuf.st_size);
	D_PRINT("\t  File type and mode  =%x \r\n", stbuf.st_mode);
	D_PRINT("---------------pmfs stat done---------------\r\n");
	/* start release tmp obj start */

	rc = pmfs_release_start(tmp_obj);
	if (rc != 0) {
		D_PRINT("pmfs release tmp_obj start failed\r\n");
		return rc;
	}
	D_PRINT("---------------pmfs release tmp_obj done---------------\r\n");
	/* start release obj start */
	rc = pmfs_release_start(obj);
	if (rc != 0) {
		D_PRINT("pmfs release start failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs release done---------------\r\n");
	/* start create a file and open */
	D_PRINT("---------------pmfs open a file-----------------\r\n");
	rc = pmfs_open_start(pmfs, NULL, "pmfs.c", S_IFREG, O_RDWR | O_CREAT,
			     1024, "sssss", &obj);
	if (rc != 0) {
		D_PRINT("pmfs open file start failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs open a file done -----------------\r\n");
	d_sg_list_t	user_sgl;

	/* using pmfs buffer render */
	rc = pmfs_write_start(pmfs, obj, &user_sgl, 20000);
	if (rc != 0) {
		D_PRINT("pmfs write file start failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs write file done -----------------\r\n");
	/* start to punch a file start offset and len */
	rc = pmfs_punch_start(pmfs, obj, 1000, 24);
	if (rc != 0) {
		D_PRINT("pmfs punch file failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs punch file done-----------------\r\n");

	/* start to read file start offset and len */
	daos_size_t read_size;

	rc = pmfs_read_start(pmfs, obj, &user_sgl, 1000, &read_size);
	if (rc != 0) {
		D_PRINT("pmfs read file failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs read file done-----------------\r\n");

	/* start open a symbolic link */
	D_PRINT("---------------pmfs open a symbolic-----------------\r\n");
	rc = pmfs_open_start(pmfs, NULL, "pmfs.c", /*O_RDWR |*/ S_IFLNK,
			O_RDWR |  O_CREAT, 1024, "sssss", &obj);
	if (rc != 0) {
		D_PRINT("pmfs open file start failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs open a symbolic link done -------------\r\n");

	/* start umount thread */
	rc = pmfs_umount_start(pmfs);
	if (rc != 0) {
		D_PRINT("pmfs umount start failed\r\n");
		return rc;
	}

	D_PRINT("---------------pmfs umount done---------------\r\n");
	D_PRINT("test app thread start function ok\r\n");

	return 0;
}

static void
pmfs_print_usage(void)
{
	D_PRINT("-f -- input mkfs cmd type \r\n");
	D_PRINT("-m -- input context \r\n");
}

static struct option pmfs_opts[] = {
	{"mount", required_argument, NULL, 'm'},
	{"mkfs", no_argument, NULL, 'f'},
	{NULL, 0, NULL, 0},
};

static const char perf_pmfs_optstr[] = "m:fz";

int
main(int argc, char **argv)
{
	int rc;

	g_vfca = calloc(1, sizeof(struct vos_fs_cmd_args));
	if (!g_vfca) {
		printf("cannot allocate memory for fs cmd\r\n");
		exit(-1);
	}

	g_vfca->pmfs_ctx = pmfs_set_ctx();

	g_vfca->vfcmd = NULL;
	D_ALLOC(g_vfca->duration, sizeof(g_vfca->duration));
	D_ASSERT(g_vfca->duration != NULL);

	while ((rc =
		getopt_long (argc, argv, perf_pmfs_optstr, pmfs_opts,
			     NULL)) != -1) {
		switch (rc) {
		case 'm':
			break;
		case 'f':
			    g_vfca->vfcmd = "PMFS_MKFS";
			break;
		default:
			    fprintf(stderr, "unknown option\r\n");
			    pmfs_print_usage();
			return -1;
		}
	}

	/* Start to init process env */
	vos_task_process_init(g_vfca);
	pmfs_combine_pool_fini_list(&g_test_fini_list);
	D_ALLOC(g_vfca->vct, sizeof(g_vfca->vct));
	D_ASSERT(g_vfca->vct != NULL);

	daos_debug_init(DAOS_LOG_DEFAULT);

	/* Start to process cmds */
	app_send_thread_cmds_in_pool();

	/* Start to finish process */

	vos_task_process_fini(g_vfca);

	D_FREE(g_vfca->duration);
	D_FREE(g_vfca->vct);
	D_FREE(g_vfca);
	return 0;
}
