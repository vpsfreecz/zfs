dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Linux 6.18+ replaced file_operations->mmap() with ->mmap_prepare().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FILE_MMAP_PREPARE], [
	ZFS_LINUX_TEST_SRC([file_operations_mmap_prepare], [
		#include <linux/fs.h>
		#include <linux/mm.h>

		static const struct file_operations fops __attribute__ ((unused)) = {
			.mmap_prepare = generic_file_mmap_prepare,
		};
	],[
		return (0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FILE_MMAP_PREPARE], [
	AC_MSG_CHECKING([whether file_operations->mmap_prepare() is available])
	ZFS_LINUX_TEST_RESULT([file_operations_mmap_prepare], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_OPERATIONS_MMAP_PREPARE, 1,
		    [file_operations has mmap_prepare])
	], [
		AC_MSG_RESULT(no)
	])
])
