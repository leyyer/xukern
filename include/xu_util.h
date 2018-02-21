#ifndef __XU_UTIL_H__
#define __XU_UTIL_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Copy string src to buffer dst of size dsize.  At most dsize-1
 *  chars will be copied.  Always NUL terminates (unless dsize == 0).
 *  Returns strlen(src); if retval >= dsize, truncation occurred.
 */
size_t xu_strlcpy(char *dst, const char *src, size_t dsize);

char *xu_strdup(const char *src);

void xu_nonblock(int fd);

typedef void (*logger_t)(const char *, int);
logger_t xu_set_logger(logger_t subr);
int  xu_println(const char *msg, ...) ;

#ifdef __cplusplus
}
#endif
#endif

