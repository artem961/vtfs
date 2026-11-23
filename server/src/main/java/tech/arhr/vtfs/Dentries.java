package tech.arhr.vtfs;

import java.util.Optional;
import org.springframework.data.jpa.repository.JpaRepository;

interface Dentries extends JpaRepository<Dentry, Long> {
    Optional<Dentry> findByParentAndName(long parent, String name);
    Optional<Dentry> findFirstByParentAndPositionGreaterThanEqualOrderByPosition(long parent, long position);
    boolean existsByParentAndName(long parent, String name);
    boolean existsByParent(long parent);
}
