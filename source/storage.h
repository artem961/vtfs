#ifndef VTFS_STORAGE_H
#define VTFS_STORAGE_H

#include <linux/limits.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/uidgid.h>

struct vtfs_storage;

struct vtfs_inode_metadata {
    unsigned long ino;
    umode_t mode;
    kuid_t uid;
    kgid_t gid;
    unsigned int nlink;
    struct timespec64 atime, mtime, ctime;
};

struct vtfs_dentry_data {
    unsigned long ino;
    loff_t position;
    unsigned char type;
    char name[NAME_MAX + 1];
};

int vtfs_storage_init(struct vtfs_storage **out_storage, unsigned long *root_ino);
void vtfs_storage_destroy(struct vtfs_storage *storage);

int vtfs_storage_get_inode(struct vtfs_storage *storage, unsigned long ino,
                           struct vtfs_inode_metadata *metadata);
/* При вытеснении из VFS сохраняет метаданные или удаляет inode без имён. */
void vtfs_storage_release_inode(struct vtfs_storage *storage,
                                const struct vtfs_inode_metadata *metadata);

int vtfs_storage_lookup(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name, unsigned long *ino);
/* Возвращает 1 для записи и 0 в конце каталога. */
int vtfs_storage_next_dentry(struct vtfs_storage *storage, unsigned long parent_ino,
                             loff_t position, struct vtfs_dentry_data *entry);
/* При успехе записывает выделенный номер в metadata->ino. */
int vtfs_storage_create(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name, struct vtfs_inode_metadata *metadata);
int vtfs_storage_link(struct vtfs_storage *storage, unsigned long ino,
                      unsigned long parent_ino, const char *name);
int vtfs_storage_unlink(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name);
int vtfs_storage_rmdir(struct vtfs_storage *storage, unsigned long parent_ino,
                       const char *name);

#endif
