package tech.arhr.vtfs;

class FsException extends RuntimeException {
    enum Errno {
        EPERM(1), ENOENT(2), EIO(5), EACCES(13), EEXIST(17), ENOTDIR(20),
        EISDIR(21), EINVAL(22), EMLINK(31), ENAMETOOLONG(36), ENOSYS(38), ENOTEMPTY(39);

        final int code;

        Errno(int code) {
            this.code = code;
        }
    }

    final Errno errno;

    FsException(Errno errno) {
        super(errno.name());
        this.errno = errno;
    }
}
