#ifndef ROLLBACK_THREADED_H
#define ROLLBACK_THREADED_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

typedef enum rb_test_enum {
	RBT_FILE,
	RBT_DIR,
	RBT_HARDLINK,
	RBT_SYMLINK,
	RBT_TMPFILE,
	RBT_PERM_USER,
	RBT_PERM_GROUP,
	RBT_PERM_XATTR,
	RBT_PERM_XATTR_DIR,
	RBT_NEG_DENTRIES,
	RBT_OPENFD,
	RBT_OPENFD_UNLINKED,
	RBT_MAX
} rb_test_id_t;

#define RBT_FIRST	RBT_FILE

#define RBTS_MAX	64		// Maximum states one test can have

typedef struct rb_test_thread_params_struct {
	int		param_id;
	char		*param_path;
	long		param_maxruns;
	void		*param_private;

	pthread_mutex_t	status_lock;
	long 		status_runs;
} rb_test_thread_params_t;

typedef struct rb_test_state_struct {
	char	*rbts_name;
	char	*rbts_desc;
	int	(*rbts_cook)(rb_test_thread_params_t *, rb_test_id_t, int);
	int	(*rbts_verify)(rb_test_thread_params_t *, rb_test_id_t, int);
} rb_test_state_t;

typedef struct rb_test_struct {
	char	*rbt_name;
	char	*rbt_desc;

	int	(*rbt_open)(rb_test_thread_params_t *);
	int	(*rbt_close)(rb_test_thread_params_t *);

	int	rbt_statesnum;
	rb_test_state_t rbt_states[RBTS_MAX];
} rb_test_t;

int open_test(rb_test_thread_params_t *thrparams, rb_test_id_t test_id);
int close_test(rb_test_thread_params_t *thrparams, rb_test_id_t test_id);
int cook_test_state(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id);
int verify_test_state(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id);
int do_test_state(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id);
int do_test(rb_test_thread_params_t *thrparams, rb_test_id_t test_id);
void *test_thr(void *data);

long long current_time_msec(void);
int msleep(long);

extern rb_test_t *rollback_tests[RBT_MAX];

#endif
