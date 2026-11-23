package tech.arhr.vtfs;

import jakarta.persistence.*;
import static tech.arhr.vtfs.FsException.Errno.*;

@Entity
@Table(name = "inodes")
class Inode {
    static final long ROOT = 1000;
    static final int DIRECTORY = 0040000, FILE = 0100000, TYPE_MASK = 0170000;
    static final long MAX_LINKS = 0xffffffffL;

    @Id @GeneratedValue(strategy = GenerationType.IDENTITY)
    Long ino;
    int mode;
    long uid, gid, nlink, atime, mtime, ctime;

    protected Inode() {}

    Inode(int mode, long uid, long gid, long now) {
        int type = mode & TYPE_MASK;
        if (mode < 0 || mode > 0xffff || (type != DIRECTORY && type != FILE))
            throw new FsException(EINVAL);
        this.mode = mode;
        this.uid = ownerId(uid);
        this.gid = ownerId(gid);
        nlink = directory() ? 2 : 1;
        atime = mtime = ctime = now;
    }

    static long ownerId(long id) {
        if (id < 0 || id >= 0xffffffffL)
            throw new FsException(EINVAL);
        return id;
    }

    boolean directory() {
        return (mode & TYPE_MASK) == DIRECTORY;
    }

    void directoryChanged(long now) {
        mtime = ctime = now;
    }

    String metadata() {
        return ino + " " + mode + " " + uid + " " + gid + " " + nlink
                + " " + atime + " " + mtime + " " + ctime;
    }
}
