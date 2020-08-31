#ifndef ROLLBACK_TEST_FILE_H
#define ROLLBACK_TEST_FILE_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_file;

int rbt_file_open(rb_test_thread_params_t *thrparams);
int rbt_file_close(rb_test_thread_params_t *thrparams);
int rbts_file_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_file_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_file_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_file_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
