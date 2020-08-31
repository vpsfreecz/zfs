#ifndef ROLLBACK_TEST_HARDLINK_H
#define ROLLBACK_TEST_HARDLINK_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_hardlink;

int rbt_hardlink_open(rb_test_thread_params_t *thrparams);
int rbt_hardlink_close(rb_test_thread_params_t *thrparams);
int rbts_hardlink_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_hardlink_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_hardlink_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_hardlink_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
