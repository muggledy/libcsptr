#include <stdio.h>
#include "csptr_smart_ptr.h"

struct BufferBody {
    char *buffer;
    size_t size;
};

static void callback_dtor(void *ptr, void *meta) {
    (void) meta;
    struct BufferBody *ctx = ptr;
    if (ctx->buffer != NULL)
        free(ctx->buffer);
}

struct BufferBody *write_buffer(const char *bufbody, size_t init_body_len) {
    smart struct BufferBody *ctx = shared_ptr(struct BufferBody, { 0 }, callback_dtor);
    if (!ctx) // failure to allocate
        return NULL; // nothing happens, destructor is not called

    if (ctx->buffer == NULL) {
        ctx->buffer = malloc(init_body_len);
        if (ctx->buffer != NULL)
            ctx->size = init_body_len;
    } else {
        if (ctx->size < init_body_len) {
            ctx->buffer = realloc(ctx->buffer, init_body_len);
            if (ctx->buffer != NULL)
                ctx->size = init_body_len;
        }
    }
    size_t buflen = strlen(bufbody);
    if (ctx->size > buflen)
        memcpy(ctx->buffer, bufbody, buflen);
    return sref(ctx); // a new reference on bufCtx is returned, it does not get destoyed
}

void do_something(size_t init_body_len) {
    smart struct BufferBody *ctx = write_buffer("hello smart ptr.", init_body_len);
    printf("%s \n", ctx->buffer);
    // ctx is destroyed here
}

int main(void) {
    printf("Smart pointers for the (GNU) C\n");
    printf("blog: http://cpuimage.cnblogs.com/\n");
    printf("tips: u can use llvm to compile in visual studio");
    printf("download llvm: http://releases.llvm.org/download.html");
    // some_int is an unique_ptr to an int with a value of 1.
    smart int *some_int = unique_ptr(int, 1);
    printf("%p = %d\n", some_int, *some_int);
    size_t init_body_len = 4096;
    do_something(init_body_len);
    // some_int is destroyed here
    return 0;
}