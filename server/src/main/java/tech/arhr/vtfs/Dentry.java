package tech.arhr.vtfs;

import jakarta.persistence.*;

@Entity
@Table(name = "dentries")
class Dentry {
    @Id @GeneratedValue(strategy = GenerationType.IDENTITY)
    Long position;
    @Column(name = "parent_ino")
    long parent;
    String name;
    @ManyToOne(optional = false)
    @JoinColumn(name = "ino")
    Inode inode;

    protected Dentry() {}

    Dentry(long parent, String name, Inode inode) {
        this.parent = parent;
        this.name = name;
        this.inode = inode;
    }

    String listing() {
        int type = inode.directory() ? DT_DIR : DT_REG;
        return position + " " + inode.ino + " " + type + " " + name;
    }

    private static final int DT_DIR = 4, DT_REG = 8;
}
