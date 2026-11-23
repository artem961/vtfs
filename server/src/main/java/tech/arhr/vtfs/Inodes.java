package tech.arhr.vtfs;

import jakarta.persistence.LockModeType;
import org.springframework.data.jpa.repository.*;

interface Inodes extends JpaRepository<Inode, Long> {
    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @Query("select i from Inode i where i.ino = :ino")
    Inode lockByIno(long ino);
}
