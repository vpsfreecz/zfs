#ifndef ROLLBACK_TEST_NEG_DENTRIES_H
#define ROLLBACK_TEST_NEG_DENTRIES_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_neg_dentries;

int rbt_neg_dentries_open(rb_test_thread_params_t *thrparams);
int rbt_neg_dentries_close(rb_test_thread_params_t *thrparams);
int rbts_neg_dentries_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_neg_dentries_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_neg_dentries_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_neg_dentries_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
