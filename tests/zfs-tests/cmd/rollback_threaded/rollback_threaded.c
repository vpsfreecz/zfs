#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <libzfs.h>
#include <libzutil.h>

#include <sys/nvpair.h>
#include <sys/zfs_ioctl.h>

#include "rollback_threaded.h"

int debug = 0;
int zfs_fd;

pthread_mutex_t threads_lock = PTHREAD_MUTEX_INITIALIZER;
int threads_running = 0;
int threads_shutdown = 0; // Signal exit to all threads

int
open_test(rb_test_thread_params_t *thrparams, rb_test_id_t test_id)
{
	rb_test_t *test = rollback_tests[test_id];
	int err = 0;

	pthread_mutex_lock(&thrparams->status_lock);
	err = test->rbt_open(thrparams);
	pthread_mutex_unlock(&thrparams->status_lock);

	if (debug >= 2)
		printf("(%d) %s: %s = %d\n",
		    thrparams->param_id, test->rbt_name, __func__, err);
	return err;
}

int
close_test(rb_test_thread_params_t *thrparams, rb_test_id_t test_id)
{
	rb_test_t *test = rollback_tests[test_id];
	int err = 0;

	pthread_mutex_lock(&thrparams->status_lock);
	err = test->rbt_close(thrparams);
	pthread_mutex_unlock(&thrparams->status_lock);

	if (debug >= 2)
		printf("(%d) %s: %s = %d\n",
		    thrparams->param_id, test->rbt_name, __func__, err);
	return err;
}

int
cook_test_state(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	rb_test_t *test = rollback_tests[test_id];
	rb_test_state_t *state = &test->rbt_states[state_id];
	int err = 0;

	pthread_mutex_lock(&thrparams->status_lock);
	err = state->rbts_cook(thrparams, test_id, state_id);
	pthread_mutex_unlock(&thrparams->status_lock);

	if (debug >= 2)
		printf("(%d) %s: cook %s = %d\n",
		    thrparams->param_id, test->rbt_name, state->rbts_name, err);
	return err;
}

int
verify_test_state(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	rb_test_t *test = rollback_tests[test_id];
	rb_test_state_t *state = &test->rbt_states[state_id];
	int err = 0;

	pthread_mutex_lock(&thrparams->status_lock);
	err = state->rbts_verify(thrparams, test_id, state_id);
	pthread_mutex_unlock(&thrparams->status_lock);

	if (debug >= 2)
		printf("(%d) %s: cook %s = %d\n",
		    thrparams->param_id, test->rbt_name, state->rbts_name, err);
	return err;
}

int
do_test_state(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	int err = 0;

	err = cook_test_state(thrparams, test_id, state_id);
	if (err)
		goto out;

	err = verify_test_state(thrparams, test_id, state_id);
	if (err)
		goto out;

out:
	return err;
}

int
do_test(rb_test_thread_params_t *thrparams, rb_test_id_t test_id)
{
	rb_test_t *test = rollback_tests[test_id];
	int err = 0;

	if (debug)
		printf("(%d) %s: %s: BEGIN %s\n",
		    thrparams->param_id, test->rbt_name, __func__,
		    test->rbt_desc);
	err = open_test(thrparams, test_id);
	if (err)
		goto out;

	for (int state_id = 0;
	    state_id < rollback_tests[test_id]->rbt_statesnum;
	    state_id++) {
		err = do_test_state(thrparams, test_id, state_id);
		goto out;
	}

	err = close_test(thrparams, test_id);
out:
	if (debug)
		printf("(%d) %s: %s: RETURN %d\n",
		    thrparams->param_id, test->rbt_name, __func__, err);
	return err;
}

void *
test_thr(void *data)
{
	rb_test_thread_params_t *thrparams = (rb_test_thread_params_t *) data;

	long runs = 0;
	int err = 0;

	while (runs < thrparams->param_maxruns) {
		for (rb_test_id_t test_id = RBT_FIRST;
		    test_id < RBT_MAX;
		    test_id++) {
			err = do_test(thrparams, test_id);
			if (err)
				goto out;
		}

		runs++;
		pthread_mutex_lock(&thrparams->status_lock);
		thrparams->status_runs = runs;
		pthread_mutex_unlock(&thrparams->status_lock);
	}
out:
	return NULL;
}

#define THREADS_MAX	1024
#define THREADS_DEFAULT	16
#define RUNS_MAX	1024000
#define RUNS_DEFAULT	32
#define RTIME_MAX	60000L
#define RTIME_DEFAULT	250L

static void
usage(char *argv[])
{
	fprintf(stderr, "usage: %s ", argv[0]);
	fprintf(stderr, "[-m maxruns] [-t nthreads] [-r rollback_after_msec] ");
        fprintf(stderr, "[-d[d]] <dataset_name>\n");
	fprintf(stderr, "\tnthreads range: 1-%d, default:%d\n",
		THREADS_MAX, THREADS_DEFAULT);
	fprintf(stderr, "\tmaxruns range: 1-%d, default:%d\n",
		RUNS_MAX, RUNS_DEFAULT);
	fprintf(stderr, "\trollback_after_msec range: 1-%li, default:%li\n",
		RTIME_MAX, RTIME_DEFAULT);
}

pthread_t		threads[THREADS_MAX];
rb_test_thread_params_t *threads_params;

int nthreads = THREADS_DEFAULT;
long maxruns = RUNS_DEFAULT;
char *dset_name;
long rollback_after_msec = 250;

static void
parse_options(int argc, char *argv[])
{
	int c; int err = 0;
	extern char *optarg;
	extern int optind, optopt;

	while ((c = getopt(argc, argv, "r:m:t:d")) != -1) {
		switch (c) {
			case 'r':
				rollback_after_msec = atol(optarg);
				break;
			case 'm':
				maxruns = atol(optarg);
				break;
			case 't':
				nthreads = atoi(optarg);
				break;
			case 'd':
				debug++;
				break;
			case ':':
				(void) fprintf(stderr,
				    "Option -%c requires an operand\n", optopt);
				err++;
				break;
		}
		if (err) {
			usage(argv);
			exit(2);
		}
	}
	if (argc <= optind) {
		(void) fprintf(stderr, "No dataset specified\n");
		usage(argv);
		exit(2);
	}
	dset_name = argv[optind];

	if (nthreads < 1 || nthreads > THREADS_MAX) {
		fprintf(stderr, "nthreads out of range\n");
		usage(argv);
		exit(2);
	}
	if (maxruns < 1 || maxruns > RUNS_MAX) {
		fprintf(stderr, "maxruns out of range\n");
		usage(argv);
		exit(2);
	}
	if (rollback_after_msec < 0 || rollback_after_msec > RTIME_MAX) {
		fprintf(stderr, "rollback_after_msec out of range\n");
		usage(argv);
		exit(2);
	}

}

int
main(int argc, char *argv[])
{
	libzfs_handle_t *lzh;
	zfs_handle_t *dset_zh;
	char mountpoint[ZFS_MAXPROPLEN];
	int err = 0;
	long long stime, ftime, etime;

	stime = current_time_msec();

	parse_options(argc, argv);

	lzh = libzfs_init();
	zfs_fd = open(ZFS_DEV, O_RDWR);

	dset_zh = zfs_open(lzh, dset_name, ZFS_TYPE_FILESYSTEM);
	if (!dset_zh) {
		fprintf(stderr, "dataset not found: %s\n", dset_name);
		usage(argv);
		exit(3);
	}

	err = zfs_prop_get(dset_zh, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE);
	if (err) {
		fprintf(stderr,
		    "cannot figure out mountpoint for dataset: %s\n", dset_name);
		usage(argv);
		exit(4);
	}
	if (debug)
		printf("mountpoint: %s\n", mountpoint);

	threads_params = malloc(sizeof(rb_test_thread_params_t) * nthreads);
	if (!threads_params)
		exit(99);

	for (int thrn = 0; thrn < nthreads; thrn++) {
		threads_params[thrn].param_id = thrn;
		threads_params[thrn].param_maxruns = maxruns;
		pthread_mutex_init(&threads_params[thrn].status_lock, NULL);
		err = pthread_create(&threads[thrn], NULL,
		    test_thr, &threads_params[thrn]);
		if (err)
			exit(199);
	}

	if (rollback_after_msec) {
		msleep(rollback_after_msec);
	}

	for (int thrn = 0; thrn < nthreads; thrn++) {
		threads_params[thrn].param_maxruns = maxruns;
		err = pthread_join(threads[thrn], NULL);
		if (err)
			exit(199);
		pthread_mutex_destroy(&threads_params[thrn].status_lock);
	}

	ftime = current_time_msec();
	etime = ftime - stime;
	if (debug)
		printf("elapsed time: %lli msec\n", etime);

	(void) close(zfs_fd);
	libzfs_fini(lzh);
	return err;
}

long long current_time_msec(void) {
    struct timeval te; 
    gettimeofday(&te, NULL);
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
    return milliseconds;
}

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}
