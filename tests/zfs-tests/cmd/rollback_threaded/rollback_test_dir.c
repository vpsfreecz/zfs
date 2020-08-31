#include <stdio.h>
#include <stdlib.h>
#include "rollback_threaded.h"
#include "rollback_test_dir.h"

rb_test_t rollback_test_dir = {
	.rbt_name = "dir",
	.rbt_desc = "directory",
	.rbt_open = rbt_dir_open,
	.rbt_close = rbt_dir_close,
	.rbt_statesnum = 2,
	.rbt_states = {
		{
			.rbts_name = "empty",
			.rbts_desc = "empty file",
			.rbts_cook = rbts_dir_empty_cook,
			.rbts_verify = rbts_dir_empty_verify,
		},
		{
			.rbts_name = "nonempty",
			.rbts_desc = "nonempty file",
			.rbts_cook = rbts_dir_nonempty_cook,
			.rbts_verify = rbts_dir_nonempty_verify,
		},
	},
};

int
rbt_dir_open(rb_test_thread_params_t *thrparams)
{
	return 0;
}

int
rbt_dir_close(rb_test_thread_params_t *thrparams)
{
	return 0;
}

int
rbts_dir_empty_cook(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	return 0;
}

int
rbts_dir_empty_verify(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	return 0;
}

int
rbts_dir_nonempty_cook(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	return 0;
}

int
rbts_dir_nonempty_verify(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	return 0;
}

