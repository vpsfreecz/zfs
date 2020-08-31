#include "rollback_threaded.h"
#include "rollback_test_file.h"
#include "rollback_test_dir.h"
#include "rollback_test_hardlink.h"
#include "rollback_test_symlink.h"
#include "rollback_test_tmpfile.h"
#include "rollback_test_perm_user.h"
#include "rollback_test_perm_group.h"
#include "rollback_test_perm_xattr.h"
#include "rollback_test_perm_xattr_dir.h"
#include "rollback_test_neg_dentries.h"
#include "rollback_test_openfd.h"
#include "rollback_test_openfd_unlinked.h"

rb_test_t *rollback_tests[] = {
	[RBT_FILE] = &rollback_test_file,
	[RBT_DIR] = &rollback_test_dir,
	[RBT_HARDLINK] = &rollback_test_hardlink,
	[RBT_SYMLINK] = &rollback_test_symlink,
	[RBT_TMPFILE] = &rollback_test_tmpfile,
	[RBT_PERM_USER] = &rollback_test_perm_user,
	[RBT_PERM_GROUP] = &rollback_test_perm_group,
	[RBT_PERM_XATTR] = &rollback_test_perm_xattr,
	[RBT_PERM_XATTR_DIR] = &rollback_test_perm_xattr_dir,
	[RBT_NEG_DENTRIES] = &rollback_test_neg_dentries,
	[RBT_OPENFD] = &rollback_test_openfd,
	[RBT_OPENFD_UNLINKED] = &rollback_test_openfd_unlinked,
};
