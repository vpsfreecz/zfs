dnl #
dnl # Linux 5.2+ mount API modernization.
dnl #
dnl # Detect the non-GPL fs_context helpers that let a filesystem-backed
dnl # module provide file_system_type.init_fs_context/parameters, parse
dnl # monolithic and structured mount parameters, create submount contexts,
dnl # build superblocks with sget_fc(), and return kernel-created mounts with
dnl # fc_mount().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FS_CONTEXT], [
	ZFS_LINUX_TEST_SRC([fs_context_mount_api], [
		#include <linux/fs.h>
		#include <linux/fs_context.h>
		#include <linux/fs_parser.h>
		#include <linux/mount.h>
		#include <linux/string.h>

		static int test_super_fn(struct super_block *sb,
		    struct fs_context *fc)
		{ return 0; }
		static int set_super_fn(struct super_block *sb,
		    struct fs_context *fc)
		{ return set_anon_super_fc(sb, fc); }
		static int init_fs_context_fn(struct fs_context *fc)
		{ return 0; }

		static const struct fs_parameter_spec params[] = {
			fsparam_flag("foo", 0),
			{}
		};

		static struct file_system_type fst __attribute__ ((unused)) = {
			.init_fs_context = init_fs_context_fn,
			.parameters = params,
		};

		static struct fs_context *fc __attribute__ ((unused));
		static struct file_system_type *fs_type __attribute__ ((unused));
		static struct dentry *dentry __attribute__ ((unused));
		static struct super_block *sb __attribute__ ((unused));
		static struct vfsmount *mnt __attribute__ ((unused));
		static char source_name[] __attribute__ ((unused)) = "zfs";
		static struct fs_parameter source_param __attribute__ ((unused)) = {
			.key = "source",
			.type = fs_value_is_string,
			.string = source_name,
			.size = 3,
		};
	],[
		fc = fs_context_for_submount(fs_type, dentry);
		sb = sget_fc(fc, test_super_fn, set_super_fn);
		(void) vfs_parse_fs_string(fc, "source", "zfs", 3);
		(void) vfs_parse_fs_param_source(fc, &source_param);
		(void) vfs_parse_monolithic_sep(fc, NULL, strsep);
		(void) vfs_get_tree(fc);
		mnt = fc_mount(fc);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FS_CONTEXT], [
	AC_MSG_CHECKING([whether the fs_context mount API is available])
	ZFS_LINUX_TEST_RESULT([fs_context_mount_api], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT, 1,
		    [file_system_type has init_fs_context and parameters])
		AC_DEFINE(HAVE_FS_CONTEXT_FOR_SUBMOUNT, 1,
		    [fs_context_for_submount() is available])
		AC_DEFINE(HAVE_FC_MOUNT, 1,
		    [fc_mount() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
