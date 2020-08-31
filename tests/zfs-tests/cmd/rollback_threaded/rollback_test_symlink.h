#ifndef ROLLBACK_TEST_SYMLINK_H
#define ROLLBACK_TEST_SYMLINK_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_symlink;

int rbt_symlink_open(rb_test_thread_params_t *thrparams);
int rbt_symlink_close(rb_test_thread_params_t *thrparams);
int rbts_symlink_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_symlink_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_symlink_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_symlink_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
