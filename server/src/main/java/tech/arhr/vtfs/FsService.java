package tech.arhr.vtfs;

import java.nio.charset.StandardCharsets;
import java.time.Instant;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import static tech.arhr.vtfs.FsException.Errno.*;

@Service
@Transactional
class FsService {
    private final Inodes inodes;
    private final Dentries entries;

    FsService(Inodes inodes, Dentries entries) {
        this.inodes = inodes;
        this.entries = entries;
    }

    private void lockVolume() {
        inodes.lockByIno(Inode.ROOT);
    }

    Inode inode(long ino) {
        return inodes.findById(ino).orElseThrow(() -> new FsException(ENOENT));
    }

    private Inode directory(long ino) {
        Inode parent = inode(ino);
        if (!parent.directory())
            throw new FsException(ENOTDIR);
        if (parent.nlink == 0)
            throw new FsException(ENOENT);
        return parent;
    }

    private static void validateName(String name) {
        if (name.getBytes(StandardCharsets.UTF_8).length > 255)
            throw new FsException(ENAMETOOLONG);
        if (name.isEmpty() || name.contains("/") || name.indexOf(0) >= 0
                || name.equals(".") || name.equals(".."))
            throw new FsException(EINVAL);
    }

    private void requireFreeName(long parent, String name) {
        if (entries.existsByParentAndName(parent, name))
            throw new FsException(EEXIST);
    }

    private Dentry entry(long parent, String name) {
        return entries.findByParentAndName(parent, name).orElseThrow(() -> new FsException(ENOENT));
    }

    long lookup(long parent, String name) {
        validateName(name);
        directory(parent);
        return entry(parent, name).inode.ino;
    }

    String nextEntry(long parent, long position) {
        if (position < 0)
            throw new FsException(EINVAL);
        directory(parent);
        return entries.findFirstByParentAndPositionGreaterThanEqualOrderByPosition(parent, position)
                .map(Dentry::listing).orElse("");
    }

    long create(long parent, String name, int mode, long uid, long gid) {
        validateName(name);
        long now = Instant.now().getEpochSecond();
        Inode child = new Inode(mode, uid, gid, now);
        lockVolume();
        Inode dir = directory(parent);
        requireFreeName(parent, name);
        inodes.save(child);
        entries.save(new Dentry(parent, name, child));
        if (child.directory())
            dir.nlink++;
        dir.directoryChanged(now);
        return child.ino;
    }

    void link(long target, long parent, String name) {
        validateName(name);
        lockVolume();
        Inode dir = directory(parent);
        Inode child = inode(target);
        if (child.directory())
            throw new FsException(EPERM);
        if (child.nlink == 0)
            throw new FsException(ENOENT);
        if (child.nlink == Inode.MAX_LINKS)
            throw new FsException(EMLINK);
        requireFreeName(parent, name);
        entries.save(new Dentry(parent, name, child));
        child.nlink++;
        child.ctime = Instant.now().getEpochSecond();
        dir.directoryChanged(child.ctime);
    }

    void remove(long parent, String name, boolean directoryExpected) {
        validateName(name);
        lockVolume();
        Inode dir = directory(parent);
        Dentry entry = entry(parent, name);
        Inode child = entry.inode;
        if (directoryExpected != child.directory())
            throw new FsException(directoryExpected ? ENOTDIR : EISDIR);
        if (child.directory() && entries.existsByParent(child.ino))
            throw new FsException(ENOTEMPTY);
        entries.delete(entry);
        if (child.directory()) {
            child.nlink = 0;
            dir.nlink--;
        } else {
            child.nlink--;
        }
        child.ctime = Instant.now().getEpochSecond();
        dir.directoryChanged(child.ctime);
    }

    void update(long ino, Integer mode, Long uid, Long gid, Long atime, Long mtime) {
        lockVolume();
        Inode inode = inode(ino);
        if (mode == null && uid == null && gid == null && atime == null && mtime == null)
            throw new FsException(EINVAL);
        if (mode != null) {
            if (mode < 0 || mode > 0xffff || (mode & Inode.TYPE_MASK) != (inode.mode & Inode.TYPE_MASK))
                throw new FsException(EINVAL);
            inode.mode = mode;
        }
        if (uid != null) inode.uid = Inode.ownerId(uid);
        if (gid != null) inode.gid = Inode.ownerId(gid);
        if (atime != null) inode.atime = atime;
        if (mtime != null) inode.mtime = mtime;
        inode.ctime = Instant.now().getEpochSecond();
    }

    void release(long ino) {
        lockVolume();
        inodes.findById(ino).filter(inode -> inode.nlink == 0).ifPresent(inodes::delete);
    }
}
