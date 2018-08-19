
#ifndef CSPTR_SMART_PTR_IMPLEMENTATION
#define CSPTR_SMART_PTR_IMPLEMENTATION
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

/* #undef SMALLOC_FIXED_ALLOCATOR */
/* #undef CSPTR_NO_SENTINEL */

# define CSPTR_PACKAGE "csptr"
# define CSPTR_VERSION "2.0.5-7"


#ifndef CSPTR_SMART_PTR_H_
# define CSPTR_SMART_PTR_H_

# ifdef __GNUC__
#  define CSPTR_INLINE      __attribute__ ((always_inline)) inline
#  define CSPTR_MALLOC_API  __attribute__ ((malloc))
#  define CSPTR_PURE        __attribute__ ((pure))
# elif defined(_MSC_VER)
#  define CSPTR_INLINE      __forceinline
#  define CSPTR_MALLOC_API
#  define CSPTR_PURE
# else
#  define CSPTR_INLINE
#  define CSPTR_MALLOC_API
#  define CSPTR_PURE
# endif
# ifdef CSPTR_NO_SENTINEL
#  ifndef __GNUC__
#   error Variadic structure sentinels can only be disabled on a compiler supporting GNU extensions
#  endif
#  define CSPTR_SENTINEL
#  define CSPTR_SENTINEL_DEC
# else
#  define CSPTR_SENTINEL        .sentinel_ = 0,
#  define CSPTR_SENTINEL_DEC int sentinel_;
# endif

enum pointer_kind {
    UNIQUE,
    SHARED,

    ARRAY = 1 << 8
};

typedef void (*f_destructor)(void *, void *);

typedef struct {
    void *(*alloc)(size_t);

    void (*dealloc)(void *);
} s_allocator;

extern s_allocator smalloc_allocator;

typedef struct {
    CSPTR_SENTINEL_DEC
    size_t size;
    size_t nmemb;
    enum pointer_kind kind;
    f_destructor dtor;
    struct {
        const void *data;
        size_t size;
    } meta;
} s_smalloc_args;

CSPTR_PURE void *get_smart_ptr_meta(void *ptr);

void *sref(void *ptr);

CSPTR_MALLOC_API void *smalloc(s_smalloc_args *args);

void sfree(void *ptr);

void *smove_size(void *ptr, size_t size);

#  define do_smalloc(...) \
    smalloc(&(s_smalloc_args) { CSPTR_SENTINEL __VA_ARGS__ })

#  define smove(Ptr) \
    smove_size((Ptr), sizeof (*(Ptr)))

CSPTR_INLINE void sfree_stack(void *ptr) {
    union {
        void **real_ptr;
        void *ptr;
    } conv;
    conv.ptr = ptr;
    sfree(*conv.real_ptr);
    *conv.real_ptr = NULL;
}

# define ARGS_ args.dtor, { args.meta.ptr, args.meta.size }

# define smart __attribute__ ((cleanup(sfree_stack)))
# define smart_ptr(Kind, Type, ...)                                         \
    ({                                                                      \
        struct s_tmp {                                                      \
            CSPTR_SENTINEL_DEC                                              \
            __typeof__(Type) value;                                         \
            f_destructor dtor;                                              \
            struct {                                                        \
                const void *ptr;                                            \
                size_t size;                                                \
            } meta;                                                         \
        } args = {                                                          \
            CSPTR_SENTINEL                                                  \
            __VA_ARGS__                                                     \
        };                                                                  \
        const __typeof__(Type[1]) dummy;                                    \
        void *var = sizeof (dummy[0]) == sizeof (dummy)                     \
            ? do_smalloc(sizeof (Type), 0, Kind, ARGS_)                        \
            : do_smalloc(sizeof (dummy[0]),                                    \
                    sizeof (dummy) / sizeof (dummy[0]), Kind, ARGS_);       \
        if (var != NULL)                                                    \
            memcpy(var, &args.value, sizeof (Type));                        \
        var;                                                                \
    })

# define smart_arr(Kind, Type, Length, ...)                                 \
    ({                                                                      \
        struct s_tmp {                                                      \
            CSPTR_SENTINEL_DEC                                              \
            __typeof__(__typeof__(Type)[Length]) value;                     \
            f_destructor dtor;                                              \
            struct {                                                        \
                const void *ptr;                                            \
                size_t size;                                                \
            } meta;                                                         \
        } args = {                                                          \
            CSPTR_SENTINEL                                                  \
            __VA_ARGS__                                                     \
        };                                                                  \
        void *var = do_smalloc(sizeof (Type), Length, Kind, ARGS_);            \
        if (var != NULL)                                                    \
            memcpy(var, &args.value, sizeof (Type));                        \
        var;                                                                \
    })

# define shared_ptr(Type, ...) smart_ptr(SHARED, Type, __VA_ARGS__)
# define unique_ptr(Type, ...) smart_ptr(UNIQUE, Type, __VA_ARGS__)

# define shared_arr(Type, Length, ...) smart_arr(SHARED, Type, Length, __VA_ARGS__)
# define unique_arr(Type, Length, ...) smart_arr(UNIQUE, Type, Length, __VA_ARGS__)

typedef struct {
    size_t nmemb;
    size_t size;
} s_meta_array;

CSPTR_PURE size_t array_length(void *ptr);

CSPTR_PURE size_t array_type_size(void *ptr);

CSPTR_PURE void *array_user_meta(void *ptr);

typedef struct {
    enum pointer_kind kind;
    f_destructor dtor;
#ifndef NDEBUG
    void *ptr;
#endif /* !NDEBUG */
} s_meta;

typedef struct {
    enum pointer_kind kind;
    f_destructor dtor;
#ifndef NDEBUG
    void *ptr;
#endif /* !NDEBUG */
    volatile size_t ref_count;
} s_meta_shared;

#endif /* !CSPTR_SMART_PTR_H_ */


#ifdef CSPTR_SMART_PTR_IMPLEMENTATION

CSPTR_INLINE size_t align(size_t s) {
    return (s + (sizeof(char *) - 1)) & ~(sizeof(char *) - 1);
}

CSPTR_PURE CSPTR_INLINE s_meta *get_meta(void *ptr) {
    size_t *size = (size_t *) ptr - 1;
    return (s_meta *) ((char *) size - *size);
}

CSPTR_PURE size_t array_length(void *ptr) {
    s_meta_array *meta = get_smart_ptr_meta(ptr);
    return meta ? meta->nmemb : 0;
}

CSPTR_PURE size_t array_type_size(void *ptr) {
    s_meta_array *meta = get_smart_ptr_meta(ptr);
    return meta ? meta->size : 0;
}

CSPTR_PURE CSPTR_INLINE void *array_user_meta(void *ptr) {
    s_meta_array *meta = get_smart_ptr_meta(ptr);
    return meta ? meta + 1 : NULL;
}


#undef smalloc

s_allocator smalloc_allocator = {malloc, free};

#ifdef _MSC_VER
# include <windows.h>
# include <malloc.h>
#endif

#ifndef _MSC_VER

static CSPTR_INLINE size_t atomic_add(volatile size_t *count, const size_t limit, const size_t val) {
    size_t old_count, new_count;
    do {
        old_count = *count;
        if (old_count == limit)
            abort();
        new_count = old_count + val;
    } while (!__sync_bool_compare_and_swap(count, old_count, new_count));
    return new_count;
}

#endif
#ifdef _MSC_VER
#ifdef _WIN32

#define  atomic_add InterlockedIncrement
#define  atomic_sub InterlockedDecrement
#else
#define  atomic_add InterlockedIncrement64
#define  atomic_sub InterlockedDecrement64
#endif
#endif

static CSPTR_INLINE size_t atomic_increment(volatile size_t *count) {
#ifdef _MSC_VER
    return atomic_add(count);
#else
    return atomic_add(count, SIZE_MAX, 1);
#endif
}

static CSPTR_INLINE size_t atomic_decrement(volatile size_t *count) {
#ifdef _MSC_VER
    return atomic_sub(count);
#else
    return atomic_add(count, 0, -1);
#endif
}

CSPTR_PURE void *get_smart_ptr_meta(void *ptr) {
    assert((size_t) ptr == align((size_t) ptr));

    s_meta *meta = get_meta(ptr);
    assert(meta->ptr == ptr);

    size_t head_size = meta->kind & SHARED ? sizeof(s_meta_shared) : sizeof(s_meta);
    size_t *metasize = (size_t *) ptr - 1;
    if (*metasize == head_size)
        return NULL;

    return (char *) meta + head_size;
}

void *sref(void *ptr) {
    s_meta *meta = get_meta(ptr);
    assert(meta->ptr == ptr);
    assert(meta->kind & SHARED);
    atomic_increment(&((s_meta_shared *) meta)->ref_count);
    return ptr;
}

void *smove_size(void *ptr, size_t size) {
    s_meta *meta = get_meta(ptr);
    assert(meta->kind & UNIQUE);

    s_smalloc_args args;

    size_t *metasize = (size_t *) ptr - 1;
    if (meta->kind & ARRAY) {
        s_meta_array *arr_meta = get_smart_ptr_meta(ptr);
        args = (s_smalloc_args) {
                .size = arr_meta->size * arr_meta->nmemb,
                .kind = (enum pointer_kind) (SHARED | ARRAY),
                .dtor = meta->dtor,
                .meta = {arr_meta, *metasize},
        };
    } else {
        void *user_meta = get_smart_ptr_meta(ptr);
        args = (s_smalloc_args) {
                .size = size,
                .kind = SHARED,
                .dtor = meta->dtor,
                .meta = {user_meta, *metasize},
        };
    }

    void *newptr = smalloc(&args);
    memcpy(newptr, ptr, size);
    return newptr;
}

CSPTR_MALLOC_API
CSPTR_INLINE static void *alloc_entry(size_t head, size_t size, size_t metasize) {
    const size_t totalsize = head + size + metasize + sizeof(size_t);
#ifdef SMALLOC_FIXED_ALLOCATOR
    return malloc(totalsize);
#else /* !SMALLOC_FIXED_ALLOCATOR */
    return smalloc_allocator.alloc(totalsize);
#endif /* !SMALLOC_FIXED_ALLOCATOR */
}

CSPTR_INLINE static void dealloc_entry(s_meta *meta, void *ptr) {
    if (meta->dtor) {
        void *user_meta = get_smart_ptr_meta(ptr);
        if (meta->kind & ARRAY) {
            s_meta_array *arr_meta = (void *) (meta + 1);
            for (size_t i = 0; i < arr_meta->nmemb; ++i)
                meta->dtor((char *) ptr + arr_meta->size * i, user_meta);
        } else
            meta->dtor(ptr, user_meta);
    }

#ifdef SMALLOC_FIXED_ALLOCATOR
    free(meta);
#else /* !SMALLOC_FIXED_ALLOCATOR */
    smalloc_allocator.dealloc(meta);
#endif /* !SMALLOC_FIXED_ALLOCATOR */
}

CSPTR_MALLOC_API
static void *smalloc_impl(s_smalloc_args *args) {
    if (!args->size)
        return NULL;

    // align the sizes to the size of a word
    size_t aligned_metasize = align(args->meta.size);
    size_t size = align(args->size);

    size_t head_size = args->kind & SHARED ? sizeof(s_meta_shared) : sizeof(s_meta);
    s_meta_shared *ptr = alloc_entry(head_size, size, aligned_metasize);
    if (ptr == NULL)
        return NULL;

    char *shifted = (char *) ptr + head_size;
    if (args->meta.size && args->meta.data)
        memcpy(shifted, args->meta.data, args->meta.size);

    size_t *sz = (size_t *) (shifted + aligned_metasize);
    *sz = head_size + aligned_metasize;

    *(s_meta *) ptr = (s_meta) {
            .kind = args->kind,
            .dtor = args->dtor,
#ifndef NDEBUG
            .ptr = sz + 1
#endif
    };

    if (args->kind & SHARED)
        ptr->ref_count = 1;

    return sz + 1;
}

CSPTR_MALLOC_API
CSPTR_INLINE static void *smalloc_array(s_smalloc_args *args) {
    const size_t size = align(args->meta.size + sizeof(s_meta_array));
#ifdef _MSC_VER
    char *new_meta = _alloca(size);
#else
    char new_meta[size];
#endif
    s_meta_array *arr_meta = (void *) new_meta;
    *arr_meta = (s_meta_array) {
            .size = args->size,
            .nmemb = args->nmemb,
    };
    memcpy(arr_meta + 1, args->meta.data, args->meta.size);
    return smalloc_impl(&(s_smalloc_args) {
            .size = args->nmemb * args->size,
            .kind = (enum pointer_kind) (args->kind | ARRAY),
            .dtor = args->dtor,
            .meta = {&new_meta, size},
    });
}

CSPTR_MALLOC_API
void *smalloc(s_smalloc_args *args) {
    return (args->nmemb == 0 ? smalloc_impl : smalloc_array)(args);
}

void sfree(void *ptr) {
    if (!ptr) return;

    assert((size_t) ptr == align((size_t) ptr));
    s_meta *meta = get_meta(ptr);
    assert(meta->ptr == ptr);

    if (meta->kind & SHARED && atomic_decrement(&((s_meta_shared *) meta)->ref_count))
        return;

    dealloc_entry(meta, ptr);
}

#endif