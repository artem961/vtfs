#include <linux/fs.h>
#include <linux/user_namespace.h>

#include "http.h"
#include "storage.h"

static char *decimal(char buffer[24], long long value)
{
    snprintf(buffer, 24, "%lld", value);
    return buffer;
}

#define DECIMAL(value) decimal((char[24]){}, (value))

static int call_inode(const char *method, unsigned long ino, char *response,
                      size_t capacity)
{
    return vtfs_http_call(method, response, capacity, 1, "ino", DECIMAL(ino));
}

static int call_entry(const char *method, unsigned long parent_ino,
                      const char *name, char *response, size_t capacity)
{
    return vtfs_http_call(method, response, capacity, 2,
                          "parent_ino", DECIMAL(parent_ino), "name", name);
}

/* override-storage-api */
int vtfs_storage_init(struct vtfs_storage **out_storage, unsigned long *root_ino)
{
    char response[32];
    int error = vtfs_http_call("get_root_ino", response, sizeof(response), 0);

    *out_storage = NULL;
    if (error)
        return error;
    if (kstrtoul(response, 10, root_ino) || !*root_ino)
        return -EPROTO;
    return 0;
}

/* override-storage-api */
void vtfs_storage_destroy(struct vtfs_storage *storage)
{
}

/* override-storage-api */
int vtfs_storage_get_inode(struct vtfs_storage *storage, unsigned long ino,
                           struct vtfs_inode_metadata *metadata)
{
    char response[256];
    unsigned long returned_ino;
    unsigned int mode, uid, gid, nlink;
    long long atime, mtime, ctime;
    int end = 0;
    int error = call_inode("get_inode_metadata", ino, response, sizeof(response));

    if (error)
        return error;
    if (sscanf(response, "%lu %u %u %u %u %lld %lld %lld%n", &returned_ino,
               &mode, &uid, &gid, &nlink, &atime, &mtime, &ctime, &end) != 8 ||
        response[end] || returned_ino != ino || mode > U16_MAX ||
        (!S_ISDIR(mode) && !S_ISREG(mode)))
        return -EPROTO;
    *metadata = (struct vtfs_inode_metadata) {
        .ino = ino, .mode = mode,
        .uid = make_kuid(&init_user_ns, uid),
        .gid = make_kgid(&init_user_ns, gid),
        .nlink = nlink,
        .atime.tv_sec = atime, .mtime.tv_sec = mtime, .ctime.tv_sec = ctime,
    };
    return uid_valid(metadata->uid) && gid_valid(metadata->gid) ? 0 : -EPROTO;
}

/* override-storage-api */
void vtfs_storage_release_inode(struct vtfs_storage *storage,
                                const struct vtfs_inode_metadata *metadata)
{
    char response[8];
    int error;

    if (!metadata->nlink) {
        error = call_inode("release_unlinked_inode", metadata->ino,
                            response, sizeof(response));
    } else {
        error = vtfs_http_call("update_inode_attrs", response, sizeof(response), 6,
                               "ino", DECIMAL(metadata->ino), "mode", DECIMAL(metadata->mode),
                               "uid", DECIMAL(from_kuid(&init_user_ns, metadata->uid)),
                               "gid", DECIMAL(from_kgid(&init_user_ns, metadata->gid)),
                               "atime", DECIMAL(metadata->atime.tv_sec),
                               "mtime", DECIMAL(metadata->mtime.tv_sec));
    }
    if (error)
        pr_err("[vtfs_net]: inode %lu release failed: %d\n", metadata->ino, error);
}

/* override-storage-api */
int vtfs_storage_lookup(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name, unsigned long *ino)
{
    char response[32];
    int error = call_entry("lookup_entry", parent_ino, name, response, sizeof(response));

    return error ? error : kstrtoul(response, 10, ino);
}

/* override-storage-api */
int vtfs_storage_next_dentry(struct vtfs_storage *storage, unsigned long parent_ino,
                             loff_t position, struct vtfs_dentry_data *entry)
{
    char response[NAME_MAX + 96];
    unsigned int type;
    long long returned_position;
    int end = 0, error;

    error = vtfs_http_call("get_next_dir_entry", response, sizeof(response), 2,
                           "parent_ino", DECIMAL(parent_ino), "position", DECIMAL(position));
    if (error || !response[0])
        return error;
    if (sscanf(response, "%lld %lu %u%n", &returned_position, &entry->ino,
               &type, &end) != 3 || response[end] != ' ' ||
        returned_position < position || returned_position == S64_MAX ||
        (type != DT_DIR && type != DT_REG))
        return -EPROTO;
    if (strscpy(entry->name, response + end + 1, sizeof(entry->name)) <= 0)
        return -EPROTO;
    entry->position = returned_position;
    entry->type = type;
    return 1;
}

/* override-storage-api */
int vtfs_storage_create(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name, struct vtfs_inode_metadata *metadata)
{
    char response[32];
    int error;

    error = vtfs_http_call("create_entry", response, sizeof(response), 5,
                           "parent_ino", DECIMAL(parent_ino), "name", name, "mode", DECIMAL(metadata->mode),
                           "uid", DECIMAL(from_kuid(&init_user_ns, metadata->uid)),
                           "gid", DECIMAL(from_kgid(&init_user_ns, metadata->gid)));
    return error ? error : kstrtoul(response, 10, &metadata->ino);
}

/* override-storage-api */
int vtfs_storage_link(struct vtfs_storage *storage, unsigned long ino,
                      unsigned long parent_ino, const char *name)
{
    char response[8];

    return vtfs_http_call("create_hard_link", response, sizeof(response), 3,
                          "target_ino", DECIMAL(ino), "parent_ino", DECIMAL(parent_ino), "name", name);
}

/* override-storage-api */
int vtfs_storage_unlink(struct vtfs_storage *storage, unsigned long parent_ino,
                        const char *name)
{
    char response[8];

    return call_entry("unlink_file", parent_ino, name, response, sizeof(response));
}

/* override-storage-api */
int vtfs_storage_rmdir(struct vtfs_storage *storage, unsigned long parent_ino,
                       const char *name)
{
    char response[8];

    return call_entry("remove_empty_dir", parent_ino, name, response, sizeof(response));
}
