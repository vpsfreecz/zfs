#ifndef ROLLBACK_TEST_TMPFILE_H
#define ROLLBACK_TEST_TMPFILE_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_tmpfile;

int rbt_tmpfile_open(rb_test_thread_params_t *thrparams);
int rbt_tmpfile_close(rb_test_thread_params_t *thrparams);
int rbts_tmpfile_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_tmpfile_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_tmpfile_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_tmpfile_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
