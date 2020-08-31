#ifndef ROLLBACK_TEST_OPENFD_UNLINKED_H
#define ROLLBACK_TEST_OPENFD_UNLINKED_H

#include "rollback_threaded.h"

extern rb_test_t rollback_test_openfd_unlinked;

int rbt_openfd_unlinked_open(rb_test_thread_params_t *thrparams);
int rbt_openfd_unlinked_close(rb_test_thread_params_t *thrparams);
int rbts_openfd_unlinked_empty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_openfd_unlinked_empty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_openfd_unlinked_nonempty_cook(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);
int rbts_openfd_unlinked_nonempty_verify(rb_test_thread_params_t *thrparams,
    rb_test_id_t test_id, int state_id);

#endif
