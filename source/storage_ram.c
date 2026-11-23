#include <linux/fs.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/xarray.h>

#include "storage.h"

struct vtfs_ram_dentry {
    struct list_head list;
    unsigned long parent_ino;
    struct vtfs_dentry_data entry;
};

struct vtfs_storage {
    struct mutex lock;
    struct xarray inodes;
    struct list_head dentries;
    unsigned long next_ino;
    loff_t next_position;
};

/* override-storage-api */
int vtfs_storage_init(struct vtfs_storage **out_storage, unsigned long *root_ino)
{
    struct vtfs_storage *storage;
    struct vtfs_inode_metadata *root;
    struct timespec64 now = { .tv_sec = ktime_get_real_seconds() };
    int error;

    storage = kzalloc(sizeof(*storage), GFP_KERNEL);
    if (!storage)
        return -ENOMEM;

    root = kzalloc(sizeof(*root), GFP_KERNEL);
    if (!root) {
        kfree(storage);
        return -ENOMEM;
    }

    xa_init(&storage->inodes);
    root->ino = 1000;
    root->mode = S_IFDIR | 0777;
    root->uid = GLOBAL_ROOT_UID;
    root->gid = GLOBAL_ROOT_GID;
    root->nlink = 2;
    root->atime = root->mtime = root->ctime = now;

    error = xa_insert(&storage->inodes, root->ino, root, GFP_KERNEL);
    if (error) {
        xa_destroy(&storage->inodes);
        kfree(root);
        kfree(storage);
        return error;
    }

    mutex_init(&storage->lock);
    INIT_LIST_HEAD(&storage->dentries);
    storage->next_ino = root->ino + 1;
    storage->next_position = 2;
    *root_ino = root->ino;
    *out_storage = storage;
    return 0;
}

/* override-storage-api */
void vtfs_storage_destroy(struct vtfs_storage *storage)
{
    struct vtfs_ram_dentry *dentry, *next;
    struct vtfs_inode_metadata *inode;
    unsigned long ino;

    if (!storage)
        return;

    list_for_each_entry_safe(dentry, next, &storage->dentries, list) {
        list_del(&dentry->list);
        kfree(dentry);
    }
    xa_for_each(&storage->inodes, ino, inode)
        kfree(inode);
    xa_destroy(&storage->inodes);
    mutex_destroy(&storage->lock);
    kfree(storage);
}

/* override-storage-api */
int vtfs_storage_get_inode(struct vtfs_storage *storage, unsigned long ino,
                           struct vtfs_inode_metadata *metadata)
{
    struct vtfs_inode_metadata *inode;
    int error = -ENOENT;

    mutex_lock(&storage->lock);
    inode = xa_load(&storage->inodes, ino);
    if (inode) {
        *metadata = *inode;
        error = 0;
    }
    mutex_unlock(&storage->lock);
    return error;
}

/* override-storage-api */
void vtfs_storage_release_inode(struct vtfs_storage *storage,
                                const struct vtfs_inode_metadata *metadata)
{
    struct vtfs_inode_metadata *inode;

    mutex_lock(&storage->lock);
    if (metadata->nlink) {
        inode = xa_load(&storage->inodes, metadata->ino);
        if (inode)
            *inode = *metadata;
    } else {
        kfree(xa_erase(&storage->inodes, metadata->ino));
    }
    mutex_unlock(&storage->lock);
}

/* Вызывается под storage->lock. */
static struct vtfs_ram_dentry *find_dentry(struct vtfs_storage *storage,
                                           unsigned long parent_ino, const char *name)
{
    struct vtfs_ram_dentry *dentry;

    list_for_each_entry(dentry, &storage->dentries, list) {
        if (dentry->parent_ino == parent_ino && !strcmp(dentry->entry.name, name))
            return dentry;
    }
    return NULL;
}

/* override-storage-api */
int vtfs_storage_lookup(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name, unsigned long *ino)
{
    struct vtfs_ram_dentry *dentry;
    int error = -ENOENT;

    mutex_lock(&storage->lock);
    dentry = find_dentry(storage, parent_ino, name);
    if (dentry) {
        *ino = dentry->entry.ino;
        error = 0;
    }
    mutex_unlock(&storage->lock);
    return error;
}

/* override-storage-api */
int vtfs_storage_next_dentry(struct vtfs_storage *storage, unsigned long parent_ino,
                             loff_t position, struct vtfs_dentry_data *entry)
{
    struct vtfs_ram_dentry *dentry;
    int found = 0;

    mutex_lock(&storage->lock);
    list_for_each_entry(dentry, &storage->dentries, list) {
        if (dentry->parent_ino == parent_ino && dentry->entry.position >= position) {
            *entry = dentry->entry;
            found = 1;
            break;
        }
    }
    mutex_unlock(&storage->lock);
    return found;
}

/* Вызывается под storage->lock. */
static int add_dentry(struct vtfs_storage *storage, unsigned long parent_ino,
                      const char *name, const struct vtfs_inode_metadata *inode)
{
    struct vtfs_ram_dentry *dentry;

    if (find_dentry(storage, parent_ino, name))
        return -EEXIST;
    if (storage->next_position == S64_MAX)
        return -ENOSPC;

    dentry = kzalloc(sizeof(*dentry), GFP_NOFS);
    if (!dentry)
        return -ENOMEM;
    if (strscpy(dentry->entry.name, name, sizeof(dentry->entry.name)) < 0) {
        kfree(dentry);
        return -ENAMETOOLONG;
    }

    dentry->parent_ino = parent_ino;
    dentry->entry.ino = inode->ino;
    dentry->entry.type = S_ISDIR(inode->mode) ? DT_DIR : DT_REG;
    dentry->entry.position = storage->next_position++;
    list_add_tail(&dentry->list, &storage->dentries);
    return 0;
}

/* override-storage-api */
int vtfs_storage_create(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name, struct vtfs_inode_metadata *metadata)
{
    struct vtfs_inode_metadata *inode;
    int error;

    inode = kmemdup(metadata, sizeof(*inode), GFP_KERNEL);
    if (!inode)
        return -ENOMEM;

    mutex_lock(&storage->lock);
    if (storage->next_ino == ULONG_MAX) {
        error = -ENOSPC;
        goto unlock;
    }

    inode->ino = storage->next_ino;
    error = xa_insert(&storage->inodes, inode->ino, inode, GFP_NOFS);
    if (error)
        goto unlock;

    error = add_dentry(storage, parent_ino, name, inode);
    if (error)
        xa_erase(&storage->inodes, inode->ino);
    else
        metadata->ino = storage->next_ino++;

unlock:
    mutex_unlock(&storage->lock);
    if (error)
        kfree(inode);
    return error;
}

/* override-storage-api */
int vtfs_storage_link(struct vtfs_storage *storage, unsigned long ino,
                      unsigned long parent_ino, const char *name)
{
    struct vtfs_inode_metadata *inode;
    int error;

    mutex_lock(&storage->lock);
    inode = xa_load(&storage->inodes, ino);
    if (!inode)
        error = -ENOENT;
    else if (!S_ISREG(inode->mode))
        error = -EPERM;
    else
        error = add_dentry(storage, parent_ino, name, inode);
    mutex_unlock(&storage->lock);
    return error;
}

/* override-storage-api */
int vtfs_storage_unlink(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name)
{
    struct vtfs_ram_dentry *dentry;
    int error = -ENOENT;

    mutex_lock(&storage->lock);
    dentry = find_dentry(storage, parent_ino, name);
    if (dentry) {
        list_del(&dentry->list);
        kfree(dentry);
        error = 0;
    }
    mutex_unlock(&storage->lock);
    return error;
}

/* override-storage-api */
int vtfs_storage_rmdir(struct vtfs_storage *storage, unsigned long parent_ino,
                       const char *name)
{
    struct vtfs_ram_dentry *dir, *child;
    int error = -ENOENT;

    mutex_lock(&storage->lock);
    dir = find_dentry(storage, parent_ino, name);
    if (!dir)
        goto unlock;
    if (dir->entry.type != DT_DIR) {
        error = -ENOTDIR;
        goto unlock;
    }

    list_for_each_entry(child, &storage->dentries, list) {
        if (child->parent_ino == dir->entry.ino) {
            error = -ENOTEMPTY;
            goto unlock;
        }
    }

    list_del(&dir->list);
    kfree(dir);
    error = 0;

unlock:
    mutex_unlock(&storage->lock);
    return error;
}
