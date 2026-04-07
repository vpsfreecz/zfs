dnl #
dnl # file_operations->setlease() exists and matches generic_setlease().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FILE_SETLEASE], [
	ZFS_LINUX_TEST_SRC([file_operations_setlease], [
		#include <linux/fs.h>
		#include <linux/filelock.h>

		typedef typeof(((struct file_operations *)0)->setlease)
		    file_setlease_t;

		file_setlease_t file_setlease_ptr __attribute__ ((unused)) =
		    generic_setlease;
		char file_setlease_match[
		    __builtin_types_compatible_p(file_setlease_t,
		    int (*)(struct file *, int, struct file_lease **,
		    void **)) ? 1 : -1] __attribute__ ((unused));
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_FILE_SETLEASE], [
	AC_MSG_CHECKING([whether file_operations->setlease() exists])
	ZFS_LINUX_TEST_RESULT([file_operations_setlease], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_OPERATIONS_SETLEASE, 1,
		    [file_operations has setlease callback])
	], [
		AC_MSG_RESULT(no)
	])
])
