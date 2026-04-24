dnl #
dnl # Current folio kernels provide helper accessors for translating file
dnl # indices to subpages and for advancing to the next folio index. Older
dnl # folio-capable kernels may not have the whole helper set, so probe each
dnl # helper and let mm_compat.h provide equivalent fallbacks when needed.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PAGEMAP_FOLIO_INDEX_HELPERS], [
	ZFS_LINUX_TEST_SRC([pagemap_has_folio_next_index], [
		#include <linux/pagemap.h>
	],[
		struct folio *folio __attribute__ ((unused)) = NULL;
		pgoff_t index __attribute__ ((unused));

		index = folio_next_index(folio);
	])

	ZFS_LINUX_TEST_SRC([pagemap_has_folio_contains], [
		#include <linux/pagemap.h>
	],[
		struct folio *folio __attribute__ ((unused)) = NULL;
		pgoff_t index __attribute__ ((unused)) = 0;
		bool contains __attribute__ ((unused));

		contains = folio_contains(folio, index);
	])

	ZFS_LINUX_TEST_SRC([pagemap_has_folio_file_page], [
		#include <linux/pagemap.h>
	],[
		struct folio *folio __attribute__ ((unused)) = NULL;
		pgoff_t index __attribute__ ((unused)) = 0;
		struct page *page __attribute__ ((unused));

		page = folio_file_page(folio, index);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PAGEMAP_FOLIO_INDEX_HELPERS], [
	AC_MSG_CHECKING([whether folio_next_index() exists])
	ZFS_LINUX_TEST_RESULT([pagemap_has_folio_next_index], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_PAGEMAP_FOLIO_NEXT_INDEX, 1,
			[folio_next_index() exists])
	],[
		AC_MSG_RESULT([no])
	])

	AC_MSG_CHECKING([whether folio_contains() exists])
	ZFS_LINUX_TEST_RESULT([pagemap_has_folio_contains], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_PAGEMAP_FOLIO_CONTAINS, 1,
			[folio_contains() exists])
	],[
		AC_MSG_RESULT([no])
	])

	AC_MSG_CHECKING([whether folio_file_page() exists])
	ZFS_LINUX_TEST_RESULT([pagemap_has_folio_file_page], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_PAGEMAP_FOLIO_FILE_PAGE, 1,
			[folio_file_page() exists])
	],[
		AC_MSG_RESULT([no])
	])
])
