#ifndef ROLLBACK_TEST_PERM_GROUP_H
#define ROLLBACK_TEST_PERM_GROUP_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_perm_group;

int rbt_perm_group_open(rb_test_thread_params_t *thrparams);
int rbt_perm_group_close(rb_test_thread_params_t *thrparams);
int rbts_perm_group_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_group_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_group_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_group_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
