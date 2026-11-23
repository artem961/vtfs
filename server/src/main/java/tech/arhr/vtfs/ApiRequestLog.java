package tech.arhr.vtfs;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import java.io.IOException;
import java.util.concurrent.TimeUnit;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;

@Component
class ApiRequestLog extends OncePerRequestFilter {
    private static final Logger log = LoggerFactory.getLogger(ApiRequestLog.class);
    private static final String ERROR = ApiRequestLog.class.getName() + ".errno";

    static void recordError(HttpServletRequest request, FsException.Errno errno) {
        request.setAttribute(ERROR, errno);
    }

    @Override
    protected boolean shouldNotFilter(HttpServletRequest request) {
        return !request.getServletPath().startsWith("/api/");
    }

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain chain)
            throws ServletException, IOException {
        long start = System.nanoTime();
        boolean completed = false;
        try {
            chain.doFilter(request, response);
            completed = true;
        } finally {
            int status = completed ? response.getStatus() : 500;
            Object result = request.getAttribute(ERROR);
            if (result == null)
                result = status < 400 ? "OK" : "HTTP_ERROR";
            log.info("{} {} status={} result={} duration_ms={}", request.getMethod(),
                    request.getRequestURI(), status, result, TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start));
        }
    }
}
