/*
 * bin.c - binary file operations for sysfs.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#include "sysfs.h"

static int
fill_read(struct dentry *dentry, char *buffer, loff_t off, size_t count)
{
	struct bin_attribute * attr = dentry->d_fsdata;
	struct kobject * kobj = dentry->d_parent->d_fsdata;

	return attr->read(kobj, buffer, off, count);
}

static ssize_t
read(struct file * file, char * userbuf, size_t count, loff_t * off)
{
	char *buffer = file->private_data;
	struct dentry *dentry = file->f_dentry;
	int size = dentry->d_inode->i_size;
	loff_t offs = *off;
	int ret;

	if (offs > size)
		return 0;
	if (offs + count > size)
		count = size - offs;

	ret = fill_read(dentry, buffer, offs, count);
	if (ret < 0) 
		goto Done;
	count = ret;

	ret = -EFAULT;
	if (copy_to_user(userbuf, buffer + offs, count) != 0)
		goto Done;

	*off = offs + count;
	ret = count;

 Done:
	return ret;
}

static int
flush_write(struct dentry *dentry, char *buffer, loff_t offset, size_t count)
{
	struct bin_attribute *attr = dentry->d_fsdata;
	struct kobject *kobj = dentry->d_parent->d_fsdata;

	return attr->write(kobj, buffer, offset, count);
}

static ssize_t write(struct file * file, const char * userbuf,
		     size_t count, loff_t * off)
{
	char *buffer = file->private_data;
	struct dentry *dentry = file->f_dentry;
	int size = dentry->d_inode->i_size;
	loff_t offs = *off;
	int ret;

	if (offs > size)
		return 0;
	if (offs + count > size)
		count = size - offs;

	ret = -EFAULT;
	if (copy_from_user(buffer + offs, userbuf, count))
		goto Done;

	count = flush_write(dentry, buffer, offs, count);
	if (count > 0)
		*off = offs + count;
	ret = 0;
 Done:
	return ret;
}

static int open(struct inode * inode, struct file * file)
{
	struct kobject * kobj = kobject_get(file->f_dentry->d_parent->d_fsdata);
	struct bin_attribute * attr = file->f_dentry->d_fsdata;
	int error = -EINVAL;

	if (!kobj || !attr)
		goto Done;

	error = -EACCES;
	if ((file->f_mode & FMODE_WRITE) && !attr->write)
		goto Done;
	if ((file->f_mode & FMODE_READ) && !attr->read)
		goto Done;

	error = -ENOMEM;
	file->private_data = kmalloc(attr->size, GFP_KERNEL);
	if (!file->private_data)
		goto Done;

	error = 0;

 Done:
	if (error && kobj)
		kobject_put(kobj);
	return error;
}

static int release(struct inode * inode, struct file * file)
{
	struct kobject * kobj = file->f_dentry->d_parent->d_fsdata;
	u8 * buffer = file->private_data;

	if (kobj) 
		kobject_put(kobj);
	kfree(buffer);
	return 0;
}

static struct file_operations bin_fops = {
	.read		= read,
	.write		= write,
	.llseek		= generic_file_llseek,
	.open		= open,
	.release	= release,
};

/**
 *	sysfs_create_bin_file - create binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 *
 */

int sysfs_create_bin_file(struct kobject * kobj, struct bin_attribute * attr)
{
	struct dentry * dentry;
	struct dentry * parent;
	int error = 0;

	if (!kobj || !attr)
		return -EINVAL;

	parent = kobj->dentry;

	down(&parent->d_inode->i_sem);
	dentry = sysfs_get_dentry(parent,attr->attr.name);
	if (!IS_ERR(dentry)) {
		dentry->d_fsdata = (void *)attr;
		error = sysfs_create(dentry,
				     (attr->attr.mode & S_IALLUGO) | S_IFREG,
				     NULL);
		if (!error) {
			dentry->d_inode->i_size = attr->size;
			dentry->d_inode->i_fop = &bin_fops;
		}
	} else
		error = PTR_ERR(dentry);
	up(&parent->d_inode->i_sem);
	return error;
}


/**
 *	sysfs_remove_bin_file - remove binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 *
 */

int sysfs_remove_bin_file(struct kobject * kobj, struct bin_attribute * attr)
{
	sysfs_hash_and_remove(kobj->dentry,attr->attr.name);
	return 0;
}

EXPORT_SYMBOL(sysfs_create_bin_file);
EXPORT_SYMBOL(sysfs_remove_bin_file);
