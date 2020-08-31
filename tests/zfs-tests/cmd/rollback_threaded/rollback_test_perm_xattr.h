#ifndef ROLLBACK_TEST_PERM_XATTR_H
#define ROLLBACK_TEST_PERM_XATTR_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_perm_xattr;

int rbt_perm_xattr_open(rb_test_thread_params_t *thrparams);
int rbt_perm_xattr_close(rb_test_thread_params_t *thrparams);
int rbts_perm_xattr_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_xattr_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_xattr_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_perm_xattr_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
