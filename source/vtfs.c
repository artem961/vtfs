#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mnt_idmapping.h>
#include <linux/pagemap.h>
#include <linux/string.h>

#include "storage.h"

#ifndef MODULE_NAME
#define MODULE_NAME "vtfs"
#endif
#ifndef VTFS_MOUNT
#define VTFS_MOUNT mount_nodev
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

static const struct inode_operations vtfs_dir_inode_ops;
static const struct inode_operations vtfs_file_inode_ops;
static const struct file_operations vtfs_dir_ops;
static const struct file_operations vtfs_file_ops;

/* Метаданные inode VFS. */

static void vtfs_copy_inode_metadata(struct inode *inode,
                                     struct vtfs_inode_metadata *metadata)
{
    *metadata = (struct vtfs_inode_metadata) {
        .ino = inode->i_ino,
        .mode = inode->i_mode,
        .uid = inode->i_uid,
        .gid = inode->i_gid,
        .nlink = inode->i_nlink,
        .atime = inode_get_atime(inode),
        .mtime = inode_get_mtime(inode),
        .ctime = inode_get_ctime(inode),
    };
}

static void vtfs_set_inode_ops(struct inode *inode)
{
    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &vtfs_dir_inode_ops;
        inode->i_fop = &vtfs_dir_ops;
    } else {
        inode->i_op = &vtfs_file_inode_ops;
        inode->i_fop = &vtfs_file_ops;
    }
}

static struct inode *vtfs_get_inode(struct super_block *sb, unsigned long ino)
{
    struct vtfs_inode_metadata metadata;
    struct inode *inode = iget_locked(sb, ino);
    int error;

    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return inode;

    error = vtfs_storage_get_inode(sb->s_fs_info, ino, &metadata);
    if (error) {
        iget_failed(inode);
        return ERR_PTR(error);
    }

    inode->i_mode = metadata.mode;
    inode->i_uid = metadata.uid;
    inode->i_gid = metadata.gid;
    set_nlink(inode, metadata.nlink);
    inode_set_atime_to_ts(inode, metadata.atime);
    inode_set_mtime_to_ts(inode, metadata.mtime);
    inode_set_ctime_to_ts(inode, metadata.ctime);
    vtfs_set_inode_ops(inode);
    unlock_new_inode(inode);
    return inode;
}

/* Поиск, создание и удаление файлов. */

static struct dentry *vtfs_lookup(struct inode *parent, struct dentry *child,
                                  unsigned int flags)
{
    struct inode *inode = NULL;
    unsigned long ino;
    int error;

    if (child->d_name.len > NAME_MAX)
        return ERR_PTR(-ENAMETOOLONG);

    error = vtfs_storage_lookup(parent->i_sb->s_fs_info, parent->i_ino,
                                child->d_name.name, &ino);
    if (!error)
        inode = vtfs_get_inode(parent->i_sb, ino);
    else if (error != -ENOENT)
        return ERR_PTR(error);

    return d_splice_alias(inode, child);
}

static void vtfs_directory_changed(struct inode *dir)
{
    inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
}

static int vtfs_create_entry(struct mnt_idmap *idmap, struct inode *parent,
                             struct dentry *child, umode_t mode)
{
    struct vtfs_inode_metadata metadata;
    struct inode *inode = new_inode(parent->i_sb);
    int error;

    if (!inode)
        return -ENOMEM;

    inode_init_owner(idmap, inode, parent, mode);
    if (S_ISDIR(mode))
        set_nlink(inode, 2);
    simple_inode_init_ts(inode);
    vtfs_copy_inode_metadata(inode, &metadata);
    error = vtfs_storage_create(parent->i_sb->s_fs_info, parent->i_ino,
                                child->d_name.name, &metadata);
    if (error) {
        iput(inode);
        return error;
    }

    inode->i_ino = metadata.ino;
    vtfs_set_inode_ops(inode);
    insert_inode_hash(inode);
    d_instantiate(child, inode);
    vtfs_directory_changed(parent);
    return 0;
}

static int vtfs_create(struct mnt_idmap *idmap, struct inode *parent,
                       struct dentry *child, umode_t mode, bool excl)
{
    return vtfs_create_entry(idmap, parent, child, mode | S_IFREG);
}

static int vtfs_mkdir(struct mnt_idmap *idmap, struct inode *parent,
                      struct dentry *child, umode_t mode)
{
    int error = vtfs_create_entry(idmap, parent, child, mode | S_IFDIR);

    if (!error)
        inc_nlink(parent);
    return error;
}

static int vtfs_link(struct dentry *source, struct inode *parent, struct dentry *child)
{
    struct inode *inode = d_inode(source);
    int error;

    if (inode->i_nlink == UINT_MAX)
        return -EMLINK;
    error = vtfs_storage_link(parent->i_sb->s_fs_info, inode->i_ino,
                              parent->i_ino, child->d_name.name);
    if (error)
        return error;

    inc_nlink(inode);
    ihold(inode);
    d_instantiate(child, inode);
    inode_set_ctime_current(inode);
    vtfs_directory_changed(parent);
    return 0;
}

static int vtfs_unlink(struct inode *parent, struct dentry *child)
{
    struct inode *inode = d_inode(child);
    int error;

    error = vtfs_storage_unlink(parent->i_sb->s_fs_info, parent->i_ino,
                                child->d_name.name);
    if (error)
        return error;

    drop_nlink(inode);
    inode_set_ctime_current(inode);
    vtfs_directory_changed(parent);
    return 0;
}

static int vtfs_rmdir(struct inode *parent, struct dentry *child)
{
    struct inode *dir = d_inode(child);
    int error;

    error = vtfs_storage_rmdir(parent->i_sb->s_fs_info, parent->i_ino,
                               child->d_name.name);
    if (error)
        return error;

    clear_nlink(dir);
    drop_nlink(parent);
    inode_set_ctime_current(dir);
    vtfs_directory_changed(parent);
    return 0;
}

/* До этапа записи данных файлы могут быть только пустыми. */
static int vtfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
                        struct iattr *attr)
{
    int error = setattr_prepare(idmap, dentry, attr);

    if (error)
        return error;
    if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size)
        return -EOPNOTSUPP;
    setattr_copy(idmap, d_inode(dentry), attr);
    return 0;
}

static const struct inode_operations vtfs_dir_inode_ops = {
    .lookup = vtfs_lookup,
    .create = vtfs_create,
    .link = vtfs_link,
    .mkdir = vtfs_mkdir,
    .rmdir = vtfs_rmdir,
    .unlink = vtfs_unlink,
};

static const struct inode_operations vtfs_file_inode_ops = {
    .setattr = vtfs_setattr,
};

/* Перечисление каталога и операции открытого файла. */

static int vtfs_iterate(struct file *file, struct dir_context *ctx)
{
    struct inode *dir = file_inode(file);
    struct vtfs_dentry_data entry;
    int result;

    if (!dir_emit_dots(file, ctx))
        return 0;

    while ((result = vtfs_storage_next_dentry(dir->i_sb->s_fs_info, dir->i_ino,
                                               ctx->pos, &entry)) > 0) {
        if (!dir_emit(ctx, entry.name, strlen(entry.name), entry.ino, entry.type))
            return 0;
        ctx->pos = entry.position + 1;
    }
    return result;
}

static const struct file_operations vtfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = vtfs_iterate,
    .llseek = generic_file_llseek,
    .read = generic_read_dir,
};

static const struct file_operations vtfs_file_ops = {
    .owner = THIS_MODULE,
    .llseek = generic_file_llseek,
};

/* Очистка inode и корень экземпляра файловой системы. */

static void vtfs_evict_inode(struct inode *inode)
{
    struct vtfs_inode_metadata metadata;

    if (inode->i_ino && !is_bad_inode(inode)) {
        vtfs_copy_inode_metadata(inode, &metadata);
        vtfs_storage_release_inode(inode->i_sb->s_fs_info, &metadata);
    }
    truncate_inode_pages_final(&inode->i_data);
    clear_inode(inode);
}

static const struct super_operations vtfs_super_ops = {
    .evict_inode = vtfs_evict_inode,
};

static int vtfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct vtfs_storage *storage;
    struct inode *root;
    unsigned long root_ino;
    int error;

    error = vtfs_storage_init(&storage, &root_ino);
    if (error)
        return error;
    sb->s_fs_info = storage;
    sb->s_op = &vtfs_super_ops;

    root = vtfs_get_inode(sb, root_ino);
    if (IS_ERR(root))
        return PTR_ERR(root);
    sb->s_root = d_make_root(root);
    return sb->s_root ? 0 : -ENOMEM;
}

/* Монтирование и освобождение экземпляра VTFS. */

static struct dentry *vtfs_mount(struct file_system_type *fs_type, int flags,
                                 const char *token, void *data)
{
    struct dentry *root = VTFS_MOUNT(fs_type, flags, data, vtfs_fill_super);

    if (IS_ERR(root))
        pr_err("[" MODULE_NAME "]: Can't mount file system: %ld\n", PTR_ERR(root));
    else
        LOG("Mounted successfully\n");

    return root;
}

static void vtfs_kill_sb(struct super_block *sb)
{
    struct vtfs_storage *storage = sb->s_fs_info;

    kill_anon_super(sb);
    vtfs_storage_destroy(storage);
    LOG("Unmounted successfully\n");
}

/* Жизненный цикл модуля ядра. */

static struct file_system_type vtfs_fs_type = {
    .owner = THIS_MODULE,
    .name = MODULE_NAME,
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
};

static int __init vtfs_init(void)
{
    int error = register_filesystem(&vtfs_fs_type);

    if (error) {
        pr_err("[" MODULE_NAME "]: Registration failed: %d\n", error);
        return error;
    }

    LOG("VTFS joined the kernel\n");
    return 0;
}

static void __exit vtfs_exit(void)
{
    unregister_filesystem(&vtfs_fs_type);
    LOG("VTFS left the kernel\n");
}

module_init(vtfs_init);
module_exit(vtfs_exit);
