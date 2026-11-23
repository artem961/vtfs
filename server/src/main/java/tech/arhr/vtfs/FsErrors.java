package tech.arhr.vtfs;

import jakarta.servlet.http.HttpServletRequest;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.dao.DataAccessException;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.ServletRequestBindingException;
import org.springframework.web.method.annotation.MethodArgumentTypeMismatchException;
import org.springframework.web.bind.annotation.*;
import static tech.arhr.vtfs.FsException.Errno.*;

@RestControllerAdvice(assignableTypes = FsController.class)
class FsErrors {
    private static final Logger log = LoggerFactory.getLogger(FsErrors.class);

    @ExceptionHandler
    ResponseEntity<byte[]> filesystem(FsException error, HttpServletRequest request) {
        return reply(request, error.errno);
    }

    @ExceptionHandler({ServletRequestBindingException.class, MethodArgumentTypeMismatchException.class})
    ResponseEntity<byte[]> invalidArguments(HttpServletRequest request) {
        return reply(request, EINVAL);
    }

    @ExceptionHandler
    ResponseEntity<byte[]> database(DataAccessException error, HttpServletRequest request) {
        log.error("Filesystem database operation failed", error);
        return reply(request, EIO);
    }

    private ResponseEntity<byte[]> reply(HttpServletRequest request, FsException.Errno errno) {
        ApiRequestLog.recordError(request, errno);
        return FsReply.error(errno);
    }
}
