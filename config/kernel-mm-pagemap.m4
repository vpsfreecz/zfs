AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_PAGE_SIZE], [
	ZFS_LINUX_TEST_SRC([page_size], [
		#include <linux/mm.h>
	],[
		unsigned long s;
		s = page_size(NULL);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_MM_PAGE_SIZE], [
	AC_MSG_CHECKING([whether page_size() is available])
	ZFS_LINUX_TEST_RESULT([page_size], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MM_PAGE_SIZE, 1, [page_size() is available])
	],[
		AC_MSG_RESULT(no)
	])
])


AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_PAGE_MAPPING], [
	ZFS_LINUX_TEST_SRC([page_mapping], [
		#include <linux/pagemap.h>
	],[
		struct address_space *m;
		m = page_mapping(NULL);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_MM_PAGE_MAPPING], [
	AC_MSG_CHECKING([whether page_mapping() is available])
	ZFS_LINUX_TEST_RESULT([page_mapping], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MM_PAGE_MAPPING, 1, [page_mapping() is available])
	],[
		AC_MSG_RESULT(no)
	])
])


AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_MAPPING_SET_LARGE_FOLIOS], [
	ZFS_LINUX_TEST_SRC([mapping_set_large_folios], [
		#include <linux/pagemap.h>
	],[
		struct address_space *mapping = NULL;
		mapping_set_large_folios(mapping);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_MM_MAPPING_SET_LARGE_FOLIOS], [
	AC_MSG_CHECKING([whether mapping_set_large_folios() is available])
	ZFS_LINUX_TEST_RESULT([mapping_set_large_folios], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MAPPING_SET_LARGE_FOLIOS, 1,
		    [mapping_set_large_folios() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
