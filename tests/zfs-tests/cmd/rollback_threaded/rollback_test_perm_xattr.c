#include <stdio.h>
#include <stdlib.h>
#include "rollback_threaded.h"
#include "rollback_test_perm_xattr.h"

rb_test_t rollback_test_perm_xattr = {
	.rbt_name = "file",
	.rbt_desc = "normal files",
	.rbt_open = rbt_perm_xattr_open,
	.rbt_close = rbt_perm_xattr_close,
	.rbt_statesnum = 2,
	.rbt_states = {
		{
			.rbts_name = "empty",
			.rbts_desc = "empty file",
			.rbts_cook = rbts_perm_xattr_empty_cook,
			.rbts_verify = rbts_perm_xattr_empty_verify,
		},
		{
			.rbts_name = "nonempty",
			.rbts_desc = "nonempty file",
			.rbts_cook = rbts_perm_xattr_nonempty_cook,
			.rbts_verify = rbts_perm_xattr_nonempty_verify,
		},
	},
};

int
rbt_perm_xattr_open(rb_test_thread_params_t *thrparams)
{
	return 0;
}

int
rbt_perm_xattr_close(rb_test_thread_params_t *thrparams)
{
	return 0;
}

int
rbts_perm_xattr_empty_cook(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	return 0;
}

int
rbts_perm_xattr_empty_verify(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	return 0;
}

int
rbts_perm_xattr_nonempty_cook(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	return 0;
}

int
rbts_perm_xattr_nonempty_verify(rb_test_thread_params_t *thrparams, rb_test_id_t test_id,
    int state_id)
{
	return 0;
}

