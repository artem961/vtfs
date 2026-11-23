package tech.arhr.vtfs;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import org.springframework.http.CacheControl;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;

class FsReply {
    static ResponseEntity<byte[]> ok(Object result) {
        return encode(0, (result + "\0").getBytes(StandardCharsets.UTF_8));
    }

    static ResponseEntity<byte[]> error(FsException.Errno errno) {
        return encode(errno.code, new byte[0]);
    }

    private static ResponseEntity<byte[]> encode(int code, byte[] payload) {
        byte[] body = ByteBuffer.allocate(Long.BYTES + payload.length)
                .order(ByteOrder.LITTLE_ENDIAN).putLong(code).put(payload).array();
        return ResponseEntity.ok().contentType(MediaType.APPLICATION_OCTET_STREAM)
                .cacheControl(CacheControl.noStore()).contentLength(body.length).body(body);
    }
}
