AC_DEFUN([ZFS_AC_KERNEL_SRC_WRITEPAGE_T], [
	dnl #
	dnl # 6.3 API change
	dnl # The writepage_t function type now has its first argument as
	dnl # struct folio* instead of struct page*
	dnl #
	ZFS_LINUX_TEST_SRC([writepage_t_folio], [
		#include <linux/writeback.h>
		static int putpage(struct folio *folio,
		    struct writeback_control *wbc, void *data)
		{ return 0; }
		writepage_t func = putpage;
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_WRITEPAGE_T], [
	AC_MSG_CHECKING([whether int (*writepage_t)() takes struct folio*])
	ZFS_LINUX_TEST_RESULT([writepage_t_folio], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WRITEPAGE_T_FOLIO, 1,
		   [int (*writepage_t)() takes struct folio*])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_WRITE_CACHE_PAGES], [
	dnl #
	dnl # 6.18 API change
	dnl # write_cache_pages() has been removed.
	dnl #
	ZFS_LINUX_TEST_SRC([write_cache_pages], [
		#include <linux/writeback.h>
	], [
		(void) write_cache_pages(NULL, NULL, NULL, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_FILEMAP_GET_FOLIOS_TAG], [
	dnl #
	dnl # filemap_get_folios_tag() lets OpenZFS own the folio writeback
	dnl # iteration contract instead of delegating preparation to
	dnl # write_cache_pages().  This is available on the folio kernels where
	dnl # the mixed writeback contract matters.
	dnl #
	ZFS_LINUX_TEST_SRC([filemap_get_folios_tag], [
		#include <linux/pagemap.h>
		#include <linux/writeback.h>
	], [
		struct address_space *mapping = NULL;
		struct folio_batch fbatch;
		pgoff_t start = 0;
		pgoff_t end = 0;

		(void) filemap_get_folios_tag(mapping, &start, end,
		    PAGECACHE_TAG_TOWRITE, &fbatch);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_FOLIO_REDIRTY_FOR_WRITEPAGE], [
	dnl #
	dnl # folio_redirty_for_writepage() provides folio-sized skip
	dnl # accounting for writeback paths which decline to write a folio.
	dnl # Older kernels only provide redirty_page_for_writepage().
	dnl #
	ZFS_LINUX_TEST_SRC([folio_redirty_for_writepage], [
		#include <linux/writeback.h>
	], [
		struct writeback_control *wbc = NULL;
		struct folio *folio = NULL;

		(void) folio_redirty_for_writepage(wbc, folio);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_FILEMAP_GET_FOLIOS_TAG], [
	AC_MSG_CHECKING([whether filemap_get_folios_tag() is available])
	ZFS_LINUX_TEST_RESULT([filemap_get_folios_tag], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILEMAP_GET_FOLIOS_TAG, 1,
		    [filemap_get_folios_tag() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FOLIO_REDIRTY_FOR_WRITEPAGE], [
	AC_MSG_CHECKING([whether folio_redirty_for_writepage() is available])
	ZFS_LINUX_TEST_RESULT([folio_redirty_for_writepage], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FOLIO_REDIRTY_FOR_WRITEPAGE, 1,
		    [folio_redirty_for_writepage() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
AC_DEFUN([ZFS_AC_KERNEL_WRITE_CACHE_PAGES], [
	AC_MSG_CHECKING([whether write_cache_pages() is available])
	ZFS_LINUX_TEST_RESULT([write_cache_pages], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WRITE_CACHE_PAGES, 1,
		    [write_cache_pages() is available])
	],[
		AC_MSG_RESULT(no)
	])
])


AC_DEFUN([ZFS_AC_KERNEL_SRC_WRITEBACK], [
	ZFS_AC_KERNEL_SRC_WRITEPAGE_T
	ZFS_AC_KERNEL_SRC_WRITE_CACHE_PAGES
	ZFS_AC_KERNEL_SRC_FILEMAP_GET_FOLIOS_TAG
	ZFS_AC_KERNEL_SRC_FOLIO_REDIRTY_FOR_WRITEPAGE
])

AC_DEFUN([ZFS_AC_KERNEL_WRITEBACK], [
	ZFS_AC_KERNEL_WRITEPAGE_T
	ZFS_AC_KERNEL_WRITE_CACHE_PAGES
	ZFS_AC_KERNEL_FILEMAP_GET_FOLIOS_TAG
	ZFS_AC_KERNEL_FOLIO_REDIRTY_FOR_WRITEPAGE
])
