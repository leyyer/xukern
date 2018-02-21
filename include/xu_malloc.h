#ifndef __XU_MALLOC_H__
#define __XU_MALLOC_H__
#ifdef __cplusplus
extern "C" {
#endif

/* malloc wrapper */
/* see calloc(3) */
void *xu_calloc(size_t n, size_t size);
/* see malloc(3) */
#define xu_malloc(sz) xu_calloc(1, (sz))
/* see realloc(3) */
void *xu_realloc(void *p, size_t size);
/* see free(3) */
void  xu_free(void *p);

#ifdef __cplusplus
}
#endif
#endif

