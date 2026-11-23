package tech.arhr.vtfs;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import static tech.arhr.vtfs.FsReply.ok;
import static tech.arhr.vtfs.FsException.Errno.*;

@RestController
@RequestMapping("/api")
class FsController {
    private final FsService fs;
    private final byte[] token;

    FsController(FsService fs, @Value("${vtfs.token}") String token) {
        this.fs = fs;
        this.token = token.getBytes(StandardCharsets.UTF_8);
    }

    @ModelAttribute
    void authenticate(@RequestParam(defaultValue = "") String token) {
        if (!MessageDigest.isEqual(this.token, token.getBytes(StandardCharsets.UTF_8)))
            throw new FsException(EACCES);
    }

    @GetMapping("/get_root_ino")
    ResponseEntity<byte[]> root() {
        return ok(fs.inode(Inode.ROOT).ino);
    }

    @GetMapping("/get_inode_metadata")
    ResponseEntity<byte[]> metadata(@RequestParam long ino) {
        return ok(fs.inode(ino).metadata());
    }

    @GetMapping("/lookup_entry")
    ResponseEntity<byte[]> lookup(@RequestParam("parent_ino") long parent, @RequestParam String name) {
        return ok(fs.lookup(parent, name));
    }

    @GetMapping("/get_next_dir_entry")
    ResponseEntity<byte[]> nextEntry(@RequestParam("parent_ino") long parent, @RequestParam long position) {
        return ok(fs.nextEntry(parent, position));
    }

    @GetMapping("/create_entry")
    ResponseEntity<byte[]> create(@RequestParam("parent_ino") long parent, @RequestParam String name,
                                  @RequestParam int mode, @RequestParam long uid, @RequestParam long gid) {
        return ok(fs.create(parent, name, mode, uid, gid));
    }

    @GetMapping("/create_hard_link")
    ResponseEntity<byte[]> link(@RequestParam("target_ino") long target, @RequestParam("parent_ino") long parent,
                                @RequestParam String name) {
        fs.link(target, parent, name);
        return ok("");
    }

    @GetMapping("/unlink_file")
    ResponseEntity<byte[]> unlink(@RequestParam("parent_ino") long parent, @RequestParam String name) {
        fs.remove(parent, name, false);
        return ok("");
    }

    @GetMapping("/remove_empty_dir")
    ResponseEntity<byte[]> rmdir(@RequestParam("parent_ino") long parent, @RequestParam String name) {
        fs.remove(parent, name, true);
        return ok("");
    }

    @GetMapping("/update_inode_attrs")
    ResponseEntity<byte[]> update(@RequestParam long ino, @RequestParam(required = false) Integer mode,
                                  @RequestParam(required = false) Long uid, @RequestParam(required = false) Long gid,
                                  @RequestParam(required = false) Long atime, @RequestParam(required = false) Long mtime) {
        fs.update(ino, mode, uid, gid, atime, mtime);
        return ok("");
    }

    @GetMapping("/release_unlinked_inode")
    ResponseEntity<byte[]> release(@RequestParam long ino) {
        fs.release(ino);
        return ok("");
    }

    @GetMapping("/{unknown}")
    ResponseEntity<byte[]> unknownMethod() {
        throw new FsException(ENOSYS);
    }
}
