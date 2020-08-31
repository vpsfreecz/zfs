#ifndef ROLLBACK_TEST_DIR_H
#define ROLLBACK_TEST_DIR_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_dir;

int rbt_dir_open(rb_test_thread_params_t *thrparams);
int rbt_dir_close(rb_test_thread_params_t *thrparams);
int rbts_dir_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_dir_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_dir_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_dir_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
