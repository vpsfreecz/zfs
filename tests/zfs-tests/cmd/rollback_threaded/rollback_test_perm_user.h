#ifndef ROLLBACK_TEST_PERM_USER_H
#define ROLLBACK_TEST_PERM_USER_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_perm_user;

int rbt_perm_user_open(rb_test_thread_params_t *thrparams);
int rbt_perm_user_close(rb_test_thread_params_t *thrparams);
int rbts_perm_user_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_user_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_user_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_user_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
