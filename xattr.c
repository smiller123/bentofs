/*
 * Bento: Safe Rust file systems in the kernel
 * Copyright (C) 2020 Samantha Miller, Kaiyuan Zhang, Danyang Zhuo, Tom
      Anderson, Ang Chen, University of Washington
 * Copyright (C) 2001-2016  Miklos Szeredi <miklos@szeredi.hu>
 *
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
 */

#include "bento_i.h"

#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>

int bento_setxattr(struct inode *inode, const char *name, const void *value,
		  size_t size, int flags)
{
	struct bento_conn *fc = get_bento_conn(inode);
	struct fuse_setxattr_in inarg;
	int err;
	struct bento_buffer buf;

	if (fc->no_setxattr)
		return -EOPNOTSUPP;

	memset(&inarg, 0, sizeof(inarg));
	inarg.size = size;
	inarg.flags = flags;

	buf.ptr = value;
	buf.bufsize = size;
	buf.drop = false;
	err = fc->fs_ops->setxattr(inode->i_sb, get_node_id(inode), &inarg, name, &buf);
	if (err == -ENOSYS) {
		fc->no_setxattr = 1;
		err = -EOPNOTSUPP;
	}
	if (!err) {
		bento_invalidate_attr(inode);
		bento_update_ctime(inode);
	}
	return err;
}

ssize_t bento_getxattr(struct inode *inode, const char *name, void *value,
		      size_t size)
{
	struct bento_conn *fc = get_bento_conn(inode);
	BENTO_ARGS(args);
	struct fuse_getxattr_in inarg;
	struct fuse_getxattr_out outarg;
	struct bento_buffer buf;
	ssize_t ret;

	if (fc->no_getxattr)
		return -EOPNOTSUPP;

	memset(&inarg, 0, sizeof(inarg));
	inarg.size = size;
	args.in.h.opcode = FUSE_GETXATTR;
	args.in.h.nodeid = get_node_id(inode);
	args.in.numargs = 2;
	args.in.args[0].size = sizeof(inarg);
	args.in.args[0].value = &inarg;
	args.in.args[1].size = strlen(name) + 1;
	args.in.args[1].value = name;
	/* This is really two different operations rolled into one */
	args.out.numargs = 1;
	if (size) {
		args.out.argvar = 1;
		args.out.args[0].size = size;
		args.out.args[0].value = value;
	} else {
		args.out.args[0].size = sizeof(outarg);
		args.out.args[0].value = &outarg;
	}
	buf.ptr = value;
	buf.bufsize = size;
	buf.drop = false;
	ret = fc->fs_ops->getxattr(inode->i_sb, get_node_id(inode),
			&inarg, name, size, &outarg, &buf);
	if (!ret && !size)
		ret = min_t(ssize_t, outarg.size, XATTR_SIZE_MAX);
	if (ret == -ENOSYS) {
		fc->no_getxattr = 1;
		ret = -EOPNOTSUPP;
	}
	return ret;
}

static int bento_verify_xattr_list(char *list, size_t size)
{
	size_t origsize = size;

	while (size) {
		size_t thislen = strnlen(list, size);

		if (!thislen || thislen == size)
			return -EIO;

		size -= thislen + 1;
		list += thislen + 1;
	}

	return origsize;
}

ssize_t bento_listxattr(struct dentry *entry, char *list, size_t size)
{
	struct inode *inode = d_inode(entry);
	struct bento_conn *fc = get_bento_conn(inode);
	BENTO_ARGS(args);
	struct fuse_getxattr_in inarg;
	struct fuse_getxattr_out outarg;
	ssize_t ret;
	struct bento_buffer buf;

	if (!bento_allow_current_process(fc))
		return -EACCES;

	if (fc->no_listxattr)
		return -EOPNOTSUPP;

	memset(&inarg, 0, sizeof(inarg));
	inarg.size = size;
	args.in.h.opcode = FUSE_LISTXATTR;
	args.in.h.nodeid = get_node_id(inode);
	args.in.numargs = 1;
	args.in.args[0].size = sizeof(inarg);
	args.in.args[0].value = &inarg;
	/* This is really two different operations rolled into one */
	args.out.numargs = 1;
	if (size) {
		args.out.argvar = 1;
		args.out.args[0].size = size;
		args.out.args[0].value = list;
	} else {
		args.out.args[0].size = sizeof(outarg);
		args.out.args[0].value = &outarg;
	}
	buf.ptr = list;
	buf.bufsize = size;
	buf.drop = false;
	ret = fc->fs_ops->listxattr(inode->i_sb, get_node_id(inode),
			&inarg, size, &outarg, &buf);
	if (!ret && !size)
		ret = min_t(ssize_t, outarg.size, XATTR_LIST_MAX);
	if (ret > 0 && size)
		ret = bento_verify_xattr_list(list, ret);
	if (ret == -ENOSYS) {
		fc->no_listxattr = 1;
		ret = -EOPNOTSUPP;
	}
	return ret;
}

int bento_removexattr(struct inode *inode, const char *name)
{
	struct bento_conn *fc = get_bento_conn(inode);
	int err;

	if (fc->no_removexattr)
		return -EOPNOTSUPP;

	err = fc->fs_ops->removexattr(inode->i_sb, get_node_id(inode), name);
	if (err == -ENOSYS) {
		fc->no_removexattr = 1;
		err = -EOPNOTSUPP;
	}
	if (!err) {
		bento_invalidate_attr(inode);
		bento_update_ctime(inode);
	}
	return err;
}

static int bento_xattr_get(const struct xattr_handler *handler,
			 struct dentry *dentry, struct inode *inode,
			 const char *name, void *value, size_t size)
{
	return bento_getxattr(inode, name, value, size);
}

static int bento_xattr_set(const struct xattr_handler *handler,
			  struct dentry *dentry, struct inode *inode,
			  const char *name, const void *value, size_t size,
			  int flags)
{
	if (!value)
		return bento_removexattr(inode, name);

	return bento_setxattr(inode, name, value, size, flags);
}

static const struct xattr_handler bento_xattr_handler = {
	.prefix = "",
	.get    = bento_xattr_get,
	.set    = bento_xattr_set,
};

const struct xattr_handler *bento_xattr_handlers[] = {
	&bento_xattr_handler,
	NULL
};

const struct xattr_handler *bento_acl_xattr_handlers[] = {
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
	&bento_xattr_handler,
	NULL
};
