//csptr_smart_ptr.h
#ifndef CSPTR_SMART_PTR_H_
# define CSPTR_SMART_PTR_H_

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

# define CSPTR_PACKAGE "csptr"
# define CSPTR_VERSION "3.1"

/* #undef SMALLOC_FIXED_ALLOCATOR */
/* #undef CSPTR_NO_SENTINEL */
#define CSPTR_SMART_PTR_MALLOC_FREE_COUNT_DBG
//#define NDEBUG

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

#ifdef CSPTR_SMART_PTR_MALLOC_FREE_COUNT_DBG
extern volatile size_t csptr_smart_ptr_malloc_count;
extern volatile size_t csptr_smart_ptr_free_count;
#endif

typedef unsigned char  uint8_t;   //0 to 255
typedef signed char    int8_t;    //-128 to +127
typedef unsigned short uint16_t;  //0 to 65535, type int also 16bits
typedef signed short   int16_t;   //-32768 to +32767
typedef unsigned int   uint32_t;  //0 to 4294967295
typedef signed int     int32_t;   //-2147483648 to +2147483647

#if 0
typedef enum pointer_kind_ {
    UNIQUE = 1,
    SHARED = 1 << 1,

    ISFREED = 1 << 5,
    USERMETA = 1 << 6,
    ARRAY = 1 << 7
} pointer_kind;
#else
#   define pointer_kind uint8_t
#   define UNIQUE (1)
#   define SHARED (1 << 1)
#   define ISFREED  (1 << 5)
#   define USERMETA (1 << 6)
#   define ARRAY  (1 << 7)
#endif

typedef void (*f_destructor)(void *, void *);

typedef struct {
    void *(*alloc)(size_t);

    void (*dealloc)(void *);
} s_allocator;

extern s_allocator smalloc_allocator;

typedef struct {
    CSPTR_SENTINEL_DEC
    size_t size;  //要申请的单个元素的大小（单位字节）
    size_t nmemb; //要申请的元素个数
    pointer_kind kind; //SHARED或UNIQUE
    f_destructor dtor;      //析构函数地址
    struct {
        const void *data;
        size_t size;
    } meta;       //用户自定义元数据
} s_smalloc_args;

#  define do_smalloc(...) \
    smalloc(&(s_smalloc_args) { CSPTR_SENTINEL __VA_ARGS__ })

/*仅用于UNIQUE对象，需要注意的是，ptr2 = smove(ptr1)之后（smove相当于内存拷贝操作），ptr1所指UNIQUE对象并不会立即释放，
直到其离开作用域，这就会导致可能很长一段时间内ptr1和ptr2各自的UNIQUE对象同时存在造成内存浪费，smove2()就是为了解决该问题，
它会立即释放ptr1对象并将ptr1置为NULL，因此smov2()需要传入的是二级指针，即ptr2 = smove2(&ptr1)。另外smove会重新malloc
内存以及memcpy，非常消耗资源，通常没有使用的需求，通常需求都是使用SHARED对象*/
#  define smove(Ptr) \
    smove_size((Ptr), sizeof (*(Ptr)))
#  define smove2(Secondary_Ptr) smove_v2((Secondary_Ptr))

# define ARGS_ args.dtor, { args.meta.ptr, args.meta.size }

/*所谓smart智能，其实就是“autofree”，切记：__attribute__((cleanup(func)))​ 仅适用于局部变量，且它触发的清理函数func会在该局部变量的生命周期结束时被调用*/
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
            ? do_smalloc(sizeof (Type), 0, Kind, ARGS_)                     \
            : do_smalloc(sizeof (dummy[0]),                                 \
                    sizeof (dummy) / sizeof (dummy[0]), Kind, ARGS_);       \
        if (var != NULL)                                                    \
            memcpy(var, &args.value, sizeof (Type));                        \
        var;                                                                \
    })

/*
smart_ptr宏里的“sizeof (dummy[0]) == sizeof (dummy)”逻辑判断的作用是什么？
编译下面这段代码看看：
#define TTT(Type) \
({\
const __typeof__(Type[1]) dummy;\
int var = sizeof (dummy[0]) == sizeof (dummy) ? 1 : 0;\
printf("var: %d(%d==%d?)\n", var, sizeof (dummy[0]), sizeof (dummy));\
})
int main(){
    TTT(int);    //var: 1(4==4?)
    TTT(int[5]); //var: 0(4==20?)
}
由此一目了然，该逻辑判断的作用就是判断“Type”数据类型是否是数组类型。譬如我们可以使用unique_ptr宏申请单个int整数对象的内存空间，
也可以一次性申请n个int整数对象的内存空间：
smart int *single_int = unique_ptr(int, 666);
smart int *multi_ints = unique_ptr(int[5], {5, 4, 3, 2, 1});
*/
/*
smart_ptr宏里的do_smalloc()传参用于构建了一个s_smalloc_args对象，需要注意的是s_smalloc_args对象是存储于栈空间中的。s_smalloc_args对象
记录了要申请内存的申请参数，包括待申请的单个元素字节大小、申请的元素个数、析构函数地址等，其作为smalloc()的调用参数。根据申请元素个数
不同，smalloc中实际调用的api也不同：若申请元素个数为1（对应s_smalloc_args.nmemb = 0），调用的api为smalloc_impl，若申请元素个数大于1
（nmemb非0），调用的api则为smalloc_array。do_smalloc()返回的是申请下来的“动态内存对象本体”的起始地址
拿smart int *single_int = unique_ptr(int, 666)举例：其对应的s_smalloc_args对象中，待申请的单个元素是int，单个元素大小是4，申请元素个数为1，未指定析构函数
而对于smart int *multi_ints = unique_ptr(int[5], {5, 4, 3, 2, 1})：
其对应的s_smalloc_args对象中，待申请的单个元素是int，单个元素大小是4，申请元素个数则为5，亦未指定析构函数。对于这种待分配的是数组类型，也可以使用
shared_arr()或unique_arr()，两者本质相同，示例：
smart int *multi_ints = unique_arr(int, 5, {5, 4, 3, 2, 1}) //unique_arr(单个元素类型, 元素个数, {初始化元素值}, 析构函数, 用户自定义元数据)
PS：对于array，如果定义析构函数，会针对array中的每个元素都调用一次析构函数；
注意如果想要申请一个长度为1的数组，请不要使用unique_ptr(int[1])（smart_ptr宏只会创建单个元素，而非单个元素的数组），而应使用unique_arr(int, 1)
*/

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
        void *var = do_smalloc(sizeof (Type), Length, Kind, ARGS_);         \
        if (var != NULL)                                                    \
            memcpy(var, &args.value, sizeof (Type));                        \
        var;                                                                \
    })

# define shared_ptr(Type, ...) smart_ptr(SHARED, Type, __VA_ARGS__) //使用方式：shared_ptr(数据类型)、shared_ptr(数据类型,数据值)、shared_ptr(数据类型,数据值,析构函数)、shared_ptr(数据类型,数据值,析构函数,用户自定义元数据)
# define unique_ptr(Type, ...) smart_ptr(UNIQUE, Type, __VA_ARGS__) //同上

# define shared_arr(Type, Length, ...) smart_arr(SHARED, Type, Length, __VA_ARGS__)
# define unique_arr(Type, Length, ...) smart_arr(UNIQUE, Type, Length, __VA_ARGS__)

typedef struct {
    size_t nmemb; //数组中元素个数
    size_t size;  //数组中单个元素大小
} s_meta_array;

#define IS_HEAP_UNIQUE(meta) ((meta)->kind & UNIQUE) //herein meta can be s_meta or s_meta_shared
#define IS_HEAP_SHARED(meta) ((meta)->kind & SHARED)
#define IS_HEAP_ARRAY_TYPE(meta) ((meta)->kind & ARRAY)
#define IS_HEAP_HAS_USERMETA(meta) ((meta)->kind & USERMETA)
#define IS_HEAP_FREED(meta) ((meta)->kind & ISFREED)
#define IS_HEAP_VALID(meta) ((meta)->magic == MAGIC_NUM)
#define GET_USER_DATA_ALIGNED_SIZE(meta) ((meta)->user_data_size) //对齐后的用户数据总大小
#define GET_USER_DATA_ELEM_SIZE(meta) ((meta)->user_data_one_elem_size) //用户数据单个元素大小，不区别单元素数组或非数组的单元素，即不管是否是数组，都可以使用该宏，GET_USER_DATA_ELEM_SIZE(meta)*GET_USER_DATA_ELEM_NUM(meta)总是未对齐前的用户数据大小
#define GET_USER_DATA_ELEM_NUM(meta) ((meta)->user_data_elem_num) //用户数据元素个数，不区别单元素数组或非数组的单元素
#define GET_REF_COUNT_OF_SHARED_META(meta) (((s_meta_shared *)(meta))->ref_count)

#define MAGIC_NUM 0xDEADBEEF

#define STORE_OFFSET_to_META(meta, offset) { \
    (meta)->user_data_offset0 = (uint8_t)((offset)); \
    (meta)->user_data_offset1 = (uint8_t)((offset) >> 8); \
    (meta)->user_data_offset2 = (uint8_t)((offset) >> 16); \
}
#define RETRIEVE_OFFSET_from_META(meta) \
((((size_t)((meta)->user_data_offset2)) << 16) | (((size_t)((meta)->user_data_offset1)) << 8) | (size_t)((meta)->user_data_offset0))

/*UNIQUE类型内存块元数据*/
typedef struct {
    uint32_t magic;
    pointer_kind kind;
    uint8_t user_data_offset0; //offset是用户首地址相距meta首地址的偏移量，用3字节存储，即元数据总体大小不能超过2^24字节(17MB)。offset的最低字节存储在offset0中
    uint8_t user_data_offset1;
    uint8_t user_data_offset2;
    //bool is_freed; //用于避免对同一堆内存地址的double free。该字段实际很难起到规避double free的作用，改为记录到kind中
    f_destructor dtor;
    //void *ptr;     //用户数据首址，占据8字节比较浪费，改为记录3字节的偏移量offset，基于结构体内存对齐机制，magic+kind+offset整体才占8字节
    size_t user_data_size;  //动态内存对象本体大小（对齐后的总大小）
    size_t user_data_elem_num; //用户数据元素个数（非数组总是1）
    size_t user_data_one_elem_size; //用户数据单个元素大小（user_data_size - user_data_one_elem_size*user_data_elem_num就是对齐做的填充）
} s_meta;

/*SHARED类型内存块元数据*/
typedef struct {
    uint32_t magic;
    pointer_kind kind;
    uint8_t user_data_offset0;
    uint8_t user_data_offset1;
    uint8_t user_data_offset2;
    //bool is_freed;
    f_destructor dtor;
    //void *ptr;
    size_t user_data_size;
    size_t user_data_elem_num;
    size_t user_data_one_elem_size;
    /*only for shared*/
    volatile size_t ref_count; //引用计数，当对SHARED对象的引用计数>1时，调用sfree_stack(SHARED对象)是不会真的释放堆内存的，仅仅是引用计数-1
} s_meta_shared; /*s_meta_shared也可以用s_meta结构进行解析，因为一开始并不知道一个元数据是UNIQUE类型还是SHARED类型，
总是按s_meta解析，再根据s_meta->kind来判断类型，如果是SHARED类型，则再将s_meta*强转为s_meta_shared*即可*/

/*
实际malloc分配的内存布局：
|<----------------------------对齐---------------------------->|<-------对齐------>|<-------------对齐-------------->|
+-----------------------------+---------------------+---------+------------------+-----------------------+---------+
| 元数据(s_meta_shared/s_meta) | 用户自定义元数据(可选) | padding | 元数据大小(size_t) | 用户数据/动态内存对象本体 | padding |
+-----------------------------+---------------------+---------+------------------+-----------------------+---------+
                                                                                 ^ shared_ptr()/unique_ptr()返回的内存地址，根据前面的size_t内存单元
                                                                                   (metadata size)就可以找到并解析元数据了，见get_meta()

内存对齐：sizeof(char *)的整数倍，在大多数平台，sizeof(char *) == sizeof(size_t)
*/
/*
SHARED类型和UNIQUE类型的主要区别就是前者拥有一个“引用计数”，不同指针可以指向同一块内存，多少个指针指向内存块，引用计数
就有多少；而UNIQUE对象有且只能有一个指针指向它，如果想要多个指针指向它，那么必须进行针对原始UNIQUE对象执行拷贝操作，
每个指针都指向单独的UNIQUE对象(副本)。当我们首次创建了一个SHARED对象：ptr1 = shared_ptr()，之后如果还有其他指针想要指向
该SHARED对象，应当采用 ptr2 = sref(ptr1) 的方式，这样就会自动修改SHARED对象元数据中的引用计数+1，如此只有
当全部引用该SHARED对象的指针都消亡（离开作用域），该对象的堆内存
才能够被释放掉（每当一个指针消亡，都会触发autofree动作，对应引用计数-1），也就是允许了多个指针共享同一个资源，资源的
生命周期由引用计数控制；
而UNIQUE类型对象有唯一的所有者，即只有一个指针指向对象，其他指针不能共享这个资源。怎么做到呢？当首次创建一个UNIQUE对象：
ptr1 = unique_ptr()，之后如果想用另一个指针指向它，应该当采用 ptr2 = smove(ptr1) 的方式，本质上，它是重新申请了一块内存，
并复制了ptr1所指原始用户数据，ptr1指向的原始用户数据将在ptr1消亡后自动触发autofree动作释放掉
*/

#define smart_free sfree_stack

extern CSPTR_INLINE size_t align(size_t s);
extern CSPTR_PURE CSPTR_INLINE s_meta *get_meta(void *ptr);
extern CSPTR_PURE int array_length(void *ptr);
extern CSPTR_PURE int array_type_size(void *ptr);
extern CSPTR_PURE void *get_smart_ptr_meta(void *ptr);
extern void *sref(void *ptr);
extern void *smove_size(void *ptr, size_t size);
extern CSPTR_MALLOC_API void *smalloc(s_smalloc_args *args);
extern void sfree(void *ptr);
extern CSPTR_INLINE void sfree_stack(void *ptr);
extern CSPTR_PURE CSPTR_INLINE bool is_valid_heap_ptr(void *ptr);
extern CSPTR_PURE CSPTR_INLINE size_t get_head_meta_size(void *ptr);
extern CSPTR_PURE CSPTR_INLINE size_t get_total_aligned_meta_size(void *ptr);
extern void print_smart_ptr_layout(void *ptr);
extern void *smove_v2(void *ptr);
extern CSPTR_PURE CSPTR_INLINE void store_user_data_ptr(s_meta *meta, void *ptr);
extern CSPTR_PURE CSPTR_INLINE void* retrieve_user_data_ptr(s_meta *meta);
extern void print_stacktrace();

#undef smalloc

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

CSPTR_MALLOC_API
CSPTR_INLINE static void *alloc_entry(size_t head, size_t size, size_t metasize) {
    if (!head || !size) return NULL;
    const size_t totalsize = head + size + metasize + sizeof(size_t);
#ifdef SMALLOC_FIXED_ALLOCATOR
    return malloc(totalsize);
#else /* !SMALLOC_FIXED_ALLOCATOR */
    return smalloc_allocator.alloc(totalsize);
#endif /* !SMALLOC_FIXED_ALLOCATOR */
}

CSPTR_INLINE static void dealloc_entry(s_meta *meta, void *ptr) {
    size_t i = 0;

    if (!meta || !ptr) return;
    if (IS_HEAP_FREED(meta)) {
#ifndef NDEBUG
        printf("[smartptr.doublefree] memory %p has already been freed! please check code!\n", retrieve_user_data_ptr(meta)); //正常逻辑是不会出现double free的，请检查代码！
        print_stacktrace();
#endif
        return;
    }
    if (meta->dtor) {
#ifndef NDEBUG
        printf("[smartptr.sfree] call dtor(%p) func before free memory %p\n", meta->dtor, ptr);
#endif
        void *user_meta = get_smart_ptr_meta(ptr);
        if (meta->kind & ARRAY) {
            s_meta_array arr_meta = {meta->user_data_elem_num, meta->user_data_one_elem_size};
            for (i = 0; i < arr_meta.nmemb; ++i)
                meta->dtor((char *) ptr + arr_meta.size * i, user_meta);
        } else
            meta->dtor(ptr, user_meta);
    }

#ifndef NDEBUG
    printf("[smartptr.sfree] free memory %p ", ptr);
#endif
    meta->kind = meta->kind | ISFREED;
#ifdef CSPTR_SMART_PTR_MALLOC_FREE_COUNT_DBG
    csptr_smart_ptr_free_count += 1;
#endif
#ifdef SMALLOC_FIXED_ALLOCATOR
    free(meta);
#else /* !SMALLOC_FIXED_ALLOCATOR */
    smalloc_allocator.dealloc(meta);
#endif /* !SMALLOC_FIXED_ALLOCATOR */
    printf("success\n", ptr);
}

CSPTR_MALLOC_API
static void *smalloc_impl(s_smalloc_args *args) {
    if (!args->size)
        return NULL;

    size_t head_size = args->kind & SHARED ? sizeof(s_meta_shared) : sizeof(s_meta);
    // align the sizes to the size of a word
    size_t aligned_metasize = align(head_size + args->meta.size) - head_size; //用户自定义元数据对齐后大小（UNIQUE/SHARED元数据和用户自定义元数据拼接后一起做对齐）
    size_t origin_size = args->size * (args->nmemb == 0 ? 1 : args->nmemb);
    size_t size = align(origin_size); //动态内存对象本体对齐后大小
    size_t total_size = 0;

    s_meta_shared *ptr = alloc_entry(head_size, size, aligned_metasize); //分配全部内存（UNIQUE/SHARED内存元数据+用户自定义元数据+size_t(记录两个元数据总长)+动态内存对象本体，其中用户自定义元数据是可选的）
    if (ptr == NULL)
        return NULL;
    total_size = head_size + size + aligned_metasize + sizeof(size_t);
    memset(ptr, 0, total_size); //malloc不会清零初始化，这里手动做下清空

    char *shifted = (char *) ptr + head_size;
    if (args->meta.size && args->meta.data) //用户自定义元数据
        memcpy(shifted, args->meta.data, args->meta.size);

    size_t *sz = (size_t *) (shifted + aligned_metasize);
    *sz = head_size + aligned_metasize; //两个元数据对齐后的总长

    *(s_meta *) ptr = (s_meta) {
            .kind = args->kind | (args->meta.size && args->meta.data ? USERMETA : 0),
            .magic = MAGIC_NUM,
            .dtor = args->dtor,
            //.is_freed = false,
            //.ptr = sz + 1, //动态内存对象本体起始地址
            .user_data_size = size,
            .user_data_elem_num = args->nmemb == 0 ? 1 : args->nmemb,
            .user_data_one_elem_size = args->size
    }; //对“UNIQUE/SHARED内存元数据”进行赋值，记录下类型(UNIQUE or SHARED)、析构函数地址，以及可选的动态内存对象本体地址（只有启用NDEBUG宏才会记录）
    store_user_data_ptr((s_meta *)ptr, sz + 1);

    if (args->kind & SHARED)
        ptr->ref_count = 1; //SHARED内存块引用计数初始化为1

#ifndef NDEBUG
    printf("[smartptr.smalloc] malloc memory at %p with aligned size %u(B)/origin request size is %u(B), total with meta is %uB\n", 
        sz+1, size, origin_size, total_size);
#endif
#ifdef CSPTR_SMART_PTR_MALLOC_FREE_COUNT_DBG
    csptr_smart_ptr_malloc_count += 1;
#endif
    return sz + 1;
}

CSPTR_MALLOC_API
CSPTR_INLINE static void *smalloc_array(s_smalloc_args *args) {
    if (!args) return NULL;
    if (!((args->size > 0) && (args->nmemb > 0))) {
        return NULL;
    }
#if 0
    const size_t user_meta_size = args->meta.size;
#ifdef _MSC_VER
    char *new_meta = _alloca(user_meta_size);
#else
    char new_meta[user_meta_size];
#endif
    memcpy(new_meta, args->meta.data, user_meta_size);
#endif
    return smalloc_impl(&(s_smalloc_args) {
            .size = args->size,
            .nmemb = args->nmemb,
            .kind = (pointer_kind) (args->kind | ARRAY),
            .dtor = args->dtor,
#if 0
            .meta = {&new_meta, user_meta_size},
#else
            .meta = args->meta
#endif
    });
}

#endif /* !CSPTR_SMART_PTR_H_ */