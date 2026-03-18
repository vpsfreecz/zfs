dnl # SPDX-License-Identifier: CDDL-1.0
AC_DEFUN([ZFS_AC_KERNEL_SRC_FILEMAP_INVALIDATE_LOCK], [
	ZFS_LINUX_TEST_SRC([filemap_invalidate_lock], [
		#include <linux/fs.h>
	],[
		filemap_invalidate_lock(NULL);
		filemap_invalidate_unlock(NULL);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_FILEMAP_INVALIDATE_LOCK], [
	AC_MSG_CHECKING([whether filemap_invalidate_lock() is available])
	ZFS_LINUX_TEST_RESULT([filemap_invalidate_lock], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILEMAP_INVALIDATE_LOCK, 1,
		    [filemap_invalidate_lock() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
