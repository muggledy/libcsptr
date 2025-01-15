#include "csptr_smart_ptr.h"

#ifndef NDEBUG
//start print_stacktrace
#include <execinfo.h>
#include <unistd.h>

void print_stacktrace() {
    void *buffer[100];
    int size;
    int i = 0;
    
    // 获取调用栈的地址信息
    size = backtrace(buffer, 100);

    // 打印栈信息
    char **symbols = backtrace_symbols(buffer, size);
    if (symbols) {
        for (i = /*0*/1; i < size; i++) {
            printf("%s\n", symbols[i]);
        }
        free(symbols);
    }
}
//end print_stacktrace
#endif

s_allocator smalloc_allocator = {malloc, free, realloc};

#ifdef CSPTR_SMART_PTR_MALLOC_FREE_COUNT_DBG
volatile size_t csptr_smart_ptr_malloc_count = 0; //通过unique_ptr/shared_ptr/unique_arr/shared_arr接口申请的堆内存统计
volatile size_t csptr_smart_ptr_free_count = 0;   //通过smart_free接口释放的堆内存统计

__attribute__((destructor(101))) 
void csptr_smart_malloc_free_count_judge(void) { //main主函数结束后确认申请的堆内存是否全部被释放，用于检查是否有内存泄漏
    if (csptr_smart_ptr_malloc_count != csptr_smart_ptr_free_count) {
        printf("[smartptr.error] malloc memory count %u != free count %u!!! please check code!\n", 
            csptr_smart_ptr_malloc_count, csptr_smart_ptr_free_count);
    }
#ifndef NDEBUG
    else {
        printf("[smartptr] all malloc memory(%u-obj) has been released safely!\n", csptr_smart_ptr_malloc_count);
    }
#endif
}
#endif

/*注意，为了避免引起悬挂指针（野指针）问题，autofree机制传入smart_free的是 <指向所申请内存块的指针> 的指针，也就是一个二级指针，
譬如我们在函数中定义了一个智能指针/局部变量：
smart int *one_int = shared_ptr(int, 666);
在one_int指针离开作用域消亡也就是函数退出时，将自动触发smart_free(&one_int)语句的执行，当然我们完全可以在该触发之前手动调用一下
smart_free(&one_int)，没有任何问题，不会引起double free问题！要知道这个one_int就是函数栈帧中的一个内存单元，这个内存单元中存储的
就是我们所申请的堆内存的地址，现在我们获取了这个内存单元的栈地址传递给smart_free()，在smart_free()中通过*conv.real_ptr=NULL将该
内存单元中的值置为了NULL，如果不置为NULL，那么就会出现一个(野)指针指向已释放内存块的情况！如果访问这个野指针程序逻辑就会出错，如果
尝试free这个野指针，就会引起double free问题，这就是为啥要为smart_free传入一个二级指针的根本原因；
既然如此，就可以完全避免double free了么，不然！看如下代码：
smart int *one_int = shared_ptr(int, 666);
int *another_int = one_int;
smart_free(&one_int); //这样就已经释放掉内存地址=*(&one_int)处的堆内存了（假设这个内存地址为0x0000ae3df617）
smart_free(&one_int); //这里*(&one_int)将返回NULL，表示没有任何堆内存需要释放，自然不会引起任何问题
smart_free(&another_int); //*(&another_int)将返回0x0000ae3df617，于是再次尝试释放0x0000ae3df617处的堆内存，double free！crash！
理论上，我们必须使用：int *another_int = sref(one_int) 来增加引用，但万一用户疏忽了呢？
为此，我们在UNIQUE/SHARED元数据中增加is_freed字段，用于标识对应的堆内存是否已经被释放掉，是的话，则不允许double free，如此
来避免对同一内存地址的double free，但实际上，一块内存一旦被释放，可能会立即被使用，因此is_freed标记大概率会被覆盖，因此还是会出现double 
free，为此可以增加magic num，来标识一块内存是否已被其他人所申请使用，这样基本上可以避免绝大多数double free了，但还不是彻底根治：
magic不对，说明已经被其他人使用覆写；magic对、但is_freed被覆写为0，还是可能会出现double free
由于meta的获取是根据传入的用户数据指针ptr做指针偏移(前移)：-size，其中size=(*((size_t*)ptr-1))，如果这里的size被覆写了，就会导致访问到
不允许访问的内存地址导致crash（譬如size很大，前移跑到了0地址附近，显然会crash），这种可能性非常高，因此我尝试在get_meta()中增加了一个
判断以尽量避免非法内存访问：
if ((*size < sizeof(s_meta)) || (*size > 0xffffff)) {return;}
更好的内存结构设计是：[magic num1 | 元数据(长度不定) | magic num2 | meta size | 用户数据] --- 通过magic num2来确认meta size的可靠性
                                                                        ^ ptr
而我们当前的结构是：[magic num | 元数据(长度不定) | meta size | 用户数据] --- 无法确认meta size是否可靠、未被覆写
                                                        ^ ptr
由于之前我们是用了一个size_t（8字节）存储meta size，而meta size顶多3字节即可存储，因此我们可以拿出其中5字节用于存储magic num2
*/
CSPTR_INLINE void smart_free(void *ptr) {
    union {
        void **real_ptr;
        void *ptr;
    } conv;
    if (!ptr) return;
    conv.ptr = ptr;
    sfree(*conv.real_ptr);
    *conv.real_ptr = NULL;
}

/*align 对给定的大小 s 按内存对齐进行调整。具体来说，它将 s 向上对齐到 sizeof(char *) 的倍数，
在 32 位系统上，sizeof(char *) 通常是 4；在 64 位系统上，sizeof(char *) 通常是 8*/
CSPTR_PURE CSPTR_INLINE size_t align(size_t s) {
    if (!s) return 0;
    //return (s + (sizeof(char *) - 1)) & ~(sizeof(char *) - 1);
    return (s + (sizeof(size_t) - 1)) & ~(sizeof(size_t) - 1);
}

CSPTR_PURE CSPTR_INLINE bool is_valid_heap_ptr(void *ptr) { //判断是否是合法堆
    if (!ptr) return false;
    size_t *size_cell = (size_t *) ptr - 1;
#ifdef METASIZE_WITH_MAGIC
    size_t size = ((meta_size_unit *)size_cell)->meta_size;
    if (((meta_size_unit *)size_cell)->magic != MAGIC_NUM) {
        return false;
    }
#else
    size_t size = *size_cell;
#endif
    if ((size < sizeof(s_meta)) || (size > 0xffffff)) {
        return false;
    }
    s_meta *meta = (s_meta *) ((char *) size_cell - size);
    if (IS_HEAP_VALID(meta) && !IS_HEAP_FREED(meta) && (/*meta->ptr*/retrieve_user_data_ptr(meta) == ptr)) {
        return true;
    }
    return false; //入参ptr不是合法分配的堆内存首址
}

/*获取UNIQUE元数据(s_meta)，如果meta->kind & SHARED检查为true，则可以将返回值s_meta*强转为s_meta_shared*
如果是非法堆，则返回NULL*/
CSPTR_PURE CSPTR_INLINE s_meta *get_meta(void *ptr) {
    if (!ptr) return NULL;
    size_t *size_cell = (size_t *) ptr - 1;
#ifdef METASIZE_WITH_MAGIC
    size_t size = ((meta_size_unit *)size_cell)->meta_size;
    if (((meta_size_unit *)size_cell)->magic != MAGIC_NUM) {
#ifndef NDEBUG
        printf("[smartptr.error] get meta from %p failed for <invalid_meta_size:magic error>, please check code\n", ptr);
#endif
        return NULL; //这意味着meta_size已不可靠，内存已被释放或其他人写穿
    }
#else
    size_t size = *size_cell;
#endif
    bool valid = true;
    bool freed = false;
    bool badptr = false;
    if ((size < sizeof(s_meta)) || (size > 0xffffff)) { //user_data_offset占三字节，因此size不会超过2**24-1
#ifndef NDEBUG
        printf("[smartptr.error] get meta from %p failed for <invalid_meta_size:%u>, please check code\n", ptr, size);
#endif
        return NULL; //detect invalid heap
    }
    s_meta *meta = (s_meta *) ((char *) size_cell - size);
    if ((!(valid=IS_HEAP_VALID(meta))) || (freed=IS_HEAP_FREED(meta)) || (badptr=(/*meta->ptr*/retrieve_user_data_ptr(meta) != ptr))) {
#ifndef NDEBUG
        printf("[smartptr.error] get meta from %p failed for <invalid_meta:%d,freed_heap:%d,bad_userdata_ptr:%d>, please check code\n", 
            ptr, !valid, freed, badptr);
        print_stacktrace(); //gcc -g demo.c csptr_smart_ptr.c -rdynamic
#endif
        return NULL;
    }
    return meta;
}

CSPTR_PURE CSPTR_INLINE size_t get_total_aligned_meta_size(void *ptr) { //获取全部元数据大小（两个元数据的、对齐后的大小），如果是非法堆，返回0
    s_meta *meta = get_meta(ptr);
    if (!meta) {
        return 0;
    }
#ifdef METASIZE_WITH_MAGIC
    return ((meta_size_unit *)((size_t *) ptr - 1))->meta_size;
#else
    return *(size_t *)((size_t *)ptr - 1);
#endif
}

CSPTR_PURE CSPTR_INLINE size_t get_head_meta_size(void *ptr) { //获取第一个元数据大小（sizeof(s_meta) or sizeof(s_meta_shared)），非法堆返回0
    s_meta *meta = get_meta(ptr);
    if (!meta) {
        return 0;
    }
    if (IS_HEAP_UNIQUE(meta)) {
        return sizeof(s_meta);
    } else if (IS_HEAP_SHARED(meta)) {
        return sizeof(s_meta_shared);
    }
    return 0;
}

/*如果分配的是数组，则返回数组元素个数(是数组则>=1)，特别的，区分单个元素和单个元素构成的数组，尽管两者实际内存消耗一样，但后者
有(s_meta->kind & ARRAY) == true成立，这里两者在内存中的唯一区别。对于前者，该函数返回0，如果不是合法的堆内存，则返回-1*/
CSPTR_PURE CSPTR_INLINE int array_length(void *ptr) {
    s_meta *meta = get_meta(ptr);
    if (!meta) {
        return -1;
    }
    if (!IS_HEAP_ARRAY_TYPE(meta)) {
        return 0;
    }
    return GET_USER_DATA_ELEM_NUM(meta);
}

CSPTR_PURE CSPTR_INLINE int array_type_size(void *ptr) { //如果分配的是数组，返回数组中单个元素的大小，非法堆返回-1，合法但非数组类型返回0，否则将返回一个大于0的数
    s_meta *meta = get_meta(ptr);
    if (!meta) {
        return -1;
    }
    if (!IS_HEAP_ARRAY_TYPE(meta)) {
        return 0;
    }
    return GET_USER_DATA_ELEM_SIZE(meta);
}

CSPTR_PURE CSPTR_INLINE void *get_smart_ptr_meta(void *ptr) { //获取用户自定义元数据，如果非法堆或者未配置用户自定义元数据，则返回NULL
    //assert((size_t) ptr == align((size_t) ptr));
    s_meta *meta = get_meta(ptr);
    if (!meta) {
        return NULL;
    }

    size_t head_size = get_head_meta_size(ptr);
    if (!IS_HEAP_HAS_USERMETA(meta)) //如果配置用户自定义元数据的话，会在s_meta->kind中打上USERMETA标记
        return NULL;

    return (char *) meta + head_size;
}

void *sref(void *ptr) { //对于SHARED对象，增加引用计数，SHARED在内存中有且只有一份，所有引用指针均指向同一内存，非法堆或非SHARED对象，什么也不做，返回NULL
    s_meta *meta = get_meta(ptr);
    if (!meta) {
        return NULL;
    }
    if (!IS_HEAP_SHARED(meta)) {
        return NULL;
    }
    atomic_increment(&GET_REF_COUNT_OF_SHARED_META(meta));
    return ptr;
}

void *smove_size(void *ptr, size_t size) { //对于UNIQUE对象，重新申请并拷贝一份内存，非法堆或非UNIQUE对象，什么也不做，返回NULL
    s_meta *meta = get_meta(ptr);
    void *user_meta = NULL;
    size_t user_meta_size = 0;
    s_smalloc_args args;

    if (!meta) {
        return NULL;
    }
    if (!IS_HEAP_UNIQUE(meta)) {
        return NULL;
    }

    if (IS_HEAP_HAS_USERMETA(meta)) {
        user_meta_size = get_total_aligned_meta_size(ptr) - get_head_meta_size(ptr); //原有的对齐后的用户自定义元数据大小
        user_meta = get_smart_ptr_meta(ptr);
    }
    if (IS_HEAP_ARRAY_TYPE(meta)) {
        args = (s_smalloc_args) {
                .size = array_type_size(ptr),
                .nmemb = array_length(ptr),
                .kind = (pointer_kind) (UNIQUE | ARRAY),
                .dtor = meta->dtor,
                .meta = {user_meta, user_meta_size},
        };
    } else {
        assert(GET_USER_DATA_ELEM_NUM(meta)==1);
        args = (s_smalloc_args) {
                .size = GET_USER_DATA_ELEM_SIZE(meta),
                .nmemb = 0,
                .kind = UNIQUE,
                .dtor = meta->dtor,
                .meta = {user_meta, user_meta_size},
        };
    }

    void *newptr = smalloc(&args);
    if (!newptr) {
        return NULL;
    }
    memcpy(newptr, ptr, GET_USER_DATA_ALIGNED_SIZE(meta));
    return newptr;
}

void *smove_v2(void *ptr) { //在smove_size的基础上增加一点优化：立即释放内存拷贝前的旧UNIQUE对象
    union {
        void **real_ptr;
        void *ptr;
    } conv;
    if (!ptr) return;
    conv.ptr = ptr;
    void *newptr = smove(*conv.real_ptr);
    if (!newptr) {
        return NULL;
    }
    smart_free(conv.ptr); //立即释放原始unique对象
    *conv.real_ptr = NULL;
    return newptr;
}

/*鉴于UNIQUE对象只能有一个指针所指，完全可以不改变内存，仅改变指针变量，即将旧指针变量置为NULL，
ptr2 = smove_v3(ptr1); //结果：ptr2=ptr1、ptr1=NULL*/
void *smove_v3(void *ptr) {
    union {
        void **real_ptr;
        void *ptr;
    } conv;
    if (!ptr) return NULL;
    conv.ptr = ptr;
    s_meta *meta = get_meta(ptr);
    if (!meta) {
        return NULL;
    }
    if (!IS_HEAP_UNIQUE(meta)) {
        return NULL;
    }
    void *newptr = *conv.real_ptr;
    *conv.real_ptr = NULL;
    return newptr;
}

CSPTR_MALLOC_API
void *smalloc(s_smalloc_args *args) {
    if (!args) return NULL;
    return (args->nmemb == 0 ? smalloc_impl : smalloc_array)(args);
}

void sfree(void *ptr) {
    //assert((size_t) ptr == align((size_t) ptr));
    s_meta *meta = get_meta(ptr);
    if (!meta) {
        return;
    }

    if (IS_HEAP_SHARED(meta) && atomic_decrement(&GET_REF_COUNT_OF_SHARED_META(meta))) {
#ifndef NDEBUG
        printf("[smartptr.sfree] free memory %p failed for other alive ref count is %u > 0\n", 
            ptr, GET_REF_COUNT_OF_SHARED_META(meta)); //释放堆内存失败，还有ref_count个引用指向这个堆块
#endif
        return;
    }

    dealloc_entry(meta, ptr);
}

void print_smart_ptr_layout(void *ptr) {
    s_meta *meta = get_meta(ptr);
    if (!meta) return;
    size_t user_data_truth_size = GET_USER_DATA_ELEM_SIZE(meta) * GET_USER_DATA_ELEM_NUM(meta);
    size_t user_data_padding = GET_USER_DATA_ALIGNED_SIZE(meta) - user_data_truth_size;
    size_t head_meta_size = get_head_meta_size(ptr);
    size_t user_meta_size = get_total_aligned_meta_size(ptr) - head_meta_size;
    printf("[smartptr] [%p·元数据(", meta);
    if (IS_HEAP_FREED(meta)) {
        printf("Invalid|");
    }
    if (IS_HEAP_SHARED(meta)) {
        printf("SHARED-%d", GET_REF_COUNT_OF_SHARED_META(meta));
    } else {
        printf("UNIQUE");
    }
    printf("%s/%uB)", IS_HEAP_ARRAY_TYPE(meta) ? "|ARRAY" : "", head_meta_size);
    if (IS_HEAP_HAS_USERMETA(meta)) {
        printf(" | %p·用户自定义元数据(%uB)", get_smart_ptr_meta(ptr), user_meta_size); //无法判断用户自定义元数据的实际大小，因此在用户自定义元数据的情况下所追加的元数据填充无法剥离，用户必须自行记录自定义元数据的大小
    } else {
        if (user_meta_size) { //如果没有用户自定义元数据，此时元数据部分的填充大小还是可以判断出来的，但一般是0，因为结构体本身会做内存对齐
            printf(" | %p·元数据填充(%uB)", meta+head_meta_size, user_meta_size);
        }
    }
    printf(" | %p·元数据大小(%u/%uB)", ((size_t *)ptr - 1), get_total_aligned_meta_size(ptr), sizeof(meta_size_unit));
    if (IS_HEAP_ARRAY_TYPE(meta)) {
        printf(" | %p·用户数据(%ux%u/%uB)", ptr, GET_USER_DATA_ELEM_NUM(meta), GET_USER_DATA_ELEM_SIZE(meta), user_data_truth_size);
    } else {
        printf(" | %p·用户数据(%uB)", ptr, user_data_truth_size);
    }
    if (user_data_padding > 0) {
        printf(" | %p·用户数据对齐填充(%uB)", ptr+user_data_truth_size, user_data_padding);
    }
    printf("] => total %uB\n", head_meta_size+user_meta_size+sizeof(meta_size_unit)+user_data_truth_size+user_data_padding);
}

CSPTR_PURE CSPTR_INLINE void store_user_data_ptr(s_meta *meta, void *ptr) {
    if (!meta) return;
    STORE_OFFSET_to_META(meta, (size_t)(ptr-(void*)meta));
}

CSPTR_PURE CSPTR_INLINE void* retrieve_user_data_ptr(s_meta *meta) {
    if (!meta) return NULL;
    return (void*)meta+RETRIEVE_OFFSET_from_META(meta);
}

CSPTR_MALLOC_API
CSPTR_INLINE void *realloc_entry(void *meta_ptr, size_t new_total_size) {
    if (!meta_ptr || !new_total_size) return NULL;
#ifdef SMALLOC_FIXED_ALLOCATOR
    return realloc(meta_ptr, new_total_size);
#else /* !SMALLOC_FIXED_ALLOCATOR */
    return smalloc_allocator.realloc(meta_ptr, new_total_size);
#endif /* !SMALLOC_FIXED_ALLOCATOR */
}

/*考虑到realloc后的内存地址很可能会发生变化，因此若对一个SHARED对象做realloc，ptr2 = smart_realloc(ptr1)函数返回之后，指向原始SHARED首地址的那些指针（ptr0、ptr1等，其中ptr0 = sref(ptr1)）
就会变成野指针，这可以通过ptr2!=ptr1或者is_valid_heap_ptr(ptr1)判断出来，smart_realloc()中会将原始SHARED对象（ptr1所指）的is_freed字段置为true以表示废弃，
程序员需要自行将这些引用指针（ptr0、ptr1）置空（可以传入二级指针：ptr2 = smart_realloc(&ptr1)，那么smart_realloc就可以自动将ptr1置空），smart_realloc()对于返回的新SHARED对象（ptr2所指）
会自动将引用初始化为1，但假设realloc后的首地址不变，那么新的SHARED对象就还是继续保留旧的引用计数且+1，之前所有的引用指针就还是有效的；如果是对
UNIQUE对象做realloc，那么问题就简单一些，由于只有一个指针可以指向UNIQUE对象，那么smart_realloc将旧UNIQUE对象的指针置空，程序员也就不需要找出其他所有引用依次置空了。综上确定传入smart_realloc
的是一个二级指针
示例1：
ptr1 = shared_ptr(int[2], 666); //user_data_size is 8B
ptr2 = smart_realloc(&ptr1, 4); //new_user_data_size <= old_user_data_size, do nothing, don't need realloc, but add ref count from 1 to 2, ptr1 == ptr2, ptr1 and ptr2 all be valid / 如果是UNIQUE对象，则将旧指针置空，再返回原指针变量的值
示例2：
ptr1 = shared_ptr(int[2], 666); //user_data_size is 8B
ptr2 = smart_realloc(&ptr1, 256); //can't realloc (256-8) bytes after origin address, i.e., ptr2 != ptr1, the origin heap memory's meta that ptr1 points to will be set with FREED flag, 
                                  //and ptr1 will be set to NULL, the returned new heap memory meta's ref count will be reset to 1, only ptr2 is effective / 如果是UNIQUE对象，则将旧指针置空，再返回新分配内存的地址
示例3：
ptr1 = shared_ptr(int[2], 666); //user_data_size is 8B
ptr2 = smart_realloc(&ptr1, 16); //the memory of realloc is the original address, ptr2 == ptr1, and add ref count from 1 to 2, ptr1 and ptr2 all be valid / 如果是UNIQUE对象，则将旧指针置空，再返回原始内存首址
*/
void *smart_realloc(void *ptr, size_t new_user_data_size) { //ptr为二级指针
    union {
        void **real_ptr;
        void *ptr;
    } conv;
    void *newptr = NULL;
    void *newmetaptr = NULL;
    if (!ptr) return;
    conv.ptr = ptr;
    s_meta *meta = get_meta(*conv.real_ptr);
    if (!meta) {
        return NULL;
    }
    if (new_user_data_size <= GET_USER_DATA_ALIGNED_SIZE(meta)) { //no need to do realloc
        if (IS_HEAP_SHARED(meta)) {
            return sref(*conv.real_ptr);
        } else {
            newptr = *conv.real_ptr;
            *conv.real_ptr = NULL;
            return newptr;
        }
    }
    size_t new_aligned_user_data_size = align(new_user_data_size);
    size_t base_meta_size = get_total_aligned_meta_size(*conv.real_ptr)+sizeof(meta_size_unit);
    SET_HEAP_FREED(meta);
    newmetaptr = realloc_entry((void*)meta, base_meta_size+new_aligned_user_data_size);
    if (!newmetaptr) { //realloc failed, do nothing
        UNSET_HEAP_FREED(meta);
        return NULL;
    }
    
    if (newmetaptr != (void*)meta) {
        meta = NULL;
        s_meta *new_meta = (s_meta*)newmetaptr;
        UNSET_HEAP_FREED(new_meta);
        memset(newmetaptr+base_meta_size+GET_USER_DATA_ALIGNED_SIZE(new_meta), 0, 
            new_aligned_user_data_size-GET_USER_DATA_ALIGNED_SIZE(new_meta));
        new_meta->user_data_size = new_aligned_user_data_size;
        new_meta->user_data_elem_num = new_user_data_size / new_meta->user_data_one_elem_size;
        store_user_data_ptr(new_meta, newmetaptr+base_meta_size);
        newptr = retrieve_user_data_ptr(new_meta);
        if (IS_HEAP_SHARED(new_meta)) {
#ifndef NDEBUG
            printf("[smartptr.realloc] realloc from %p to %p success, total with meta is %uB, the ref count is reset to 1, ptr for smart_realloc(&ptr) has been reset to NULL, \n"
                "warning: all old ptrs that point to the old shared obj(%p) need to be reset as NULL to avoid `Dangling Pointer`, new shared obj: \n", 
                *conv.real_ptr, newptr, base_meta_size+new_aligned_user_data_size, *conv.real_ptr);
#endif
            ((s_meta_shared*)new_meta)->ref_count = 1; //如果realloc后，内存地址变化，则先前的那些引用全部作废，且ref_count清空为1，最安全的做法是需要将旧引用全部找出来置NULL，以避免野指针
        } else {
#ifndef NDEBUG
            printf("[smartptr.realloc] realloc from %p to %p success, total with meta is %uB, ptr for smart_realloc(&ptr) has been reset to NULL, new unique obj: \n", 
                *conv.real_ptr, newptr, base_meta_size+new_aligned_user_data_size);
#endif
        }
        print_smart_ptr_layout(newptr);
        *conv.real_ptr = NULL;
        return newptr;
    } else { //原址realloc
        UNSET_HEAP_FREED(meta);
        memset(((void*)meta)+base_meta_size+GET_USER_DATA_ALIGNED_SIZE(meta), 0, 
            new_aligned_user_data_size-GET_USER_DATA_ALIGNED_SIZE(meta));
        meta->user_data_size = new_aligned_user_data_size;
        meta->user_data_elem_num = new_user_data_size / meta->user_data_one_elem_size;
        newptr = *conv.real_ptr;
        if (IS_HEAP_SHARED(meta)) {
#ifndef NDEBUG
            printf("[smartptr.realloc] realloc at origin memory address %p success, total with meta is %uB, the ref count increases by 1, new shared obj: \n", 
                newptr, base_meta_size+new_aligned_user_data_size);
#endif
            atomic_increment(&GET_REF_COUNT_OF_SHARED_META(meta)); //如果realloc后，内存地址不变，则先前的那些引用仍然有效，且ref_count+1
        } else {
#ifndef NDEBUG
            printf("[smartptr.realloc] realloc at origin memory address %p success, total with meta is %uB, ptr for smart_realloc(&ptr) has been reset to NULL, new unique obj: \n", 
                newptr, base_meta_size+new_aligned_user_data_size);
#endif
            *conv.real_ptr = NULL; //对于UNIQUE对象，原始指针置空，重新返回地址赋给另一个指针变量作为唯一指向
        }
        print_smart_ptr_layout(newptr);
        return newptr;
    }
    return NULL;
}