dnl #
dnl # Linux 5.15 defines filemap_invalidate_lock
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_FILEMAP_INVALIDATE_LOCK], [
	ZFS_LINUX_TEST_SRC([vfs_has_filemap_invalidate_lock], [
		#include <linux/fs.h>
		#include <linux/pagemap.h>
		#include <linux/mm.h>
	], [
		struct address_space *mapping = NULL;
		filemap_invalidate_lock(mapping);
		filemap_invalidate_unlock(mapping);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_FILEMAP_INVALIDATE_LOCK], [
	AC_MSG_CHECKING([whether filemap_invalidate_lock exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_filemap_invalidate_lock], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_FILEMAP_INVALIDATE_LOCK, 1,
		          [filemap_invalidate_lock exists])
	], [
		AC_MSG_RESULT([no])
	])
])
