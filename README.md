single-file C Smart Pointers
================

[![Build Status](https://travis-ci.org/Snaipe/libcsptr.svg?branch=master)](https://travis-ci.org/Snaipe/libcsptr) 
[![Coverage Status](https://coveralls.io/repos/Snaipe/libcsptr/badge.svg?branch=master)](https://coveralls.io/r/Snaipe/libcsptr?branch=master) 
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat)](https://github.com/Snaipe/libcsptr/blob/master/LICENSE) 
[![Version](https://img.shields.io/github/tag/Snaipe/libcsptr.svg?label=version&style=flat)](https://github.com/Snaipe/libcsptr/releases)

## What this is

This project is an attempt to bring smart pointer constructs
to the (GNU) C programming language.

### Features

* `unique_ptr`, `shared_ptr` macros, and `smart` type attribute
* Destructor support for cleanup
* Custom variable metadata on allocation
* Cross-platform: tested under linux 3.18.6-1, Mac OS X Yosemite 10.10, and Windows 7 (use llvm in Visual Studio)


### Building from source
#### Prerequisites

To compile the library, GCC 4.6+ is needed.

## Examples

* Simple unique\_ptr:
    simple1.c:
    ```c
    #include <stdio.h>
    #include <csptr_smart_ptr.h>
    
    int main(void) {
        // some_int is an unique_ptr to an int with a value of 1.
        smart int *some_int = unique_ptr(int, 1);
    
        printf("%p = %d\n", some_int, *some_int);
    
        // some_int is destroyed here
        return 0;
    }
    ```
    Shell session:
    ```bash
    $ gcc -std=c99 -o simple1 simple1.c -lcsptr
    $ valgrind ./simple1
    ==3407== Memcheck, a memory error detector
    ==3407== Copyright (C) 2002-2013, and GNU GPL\'d, by Julian Seward et al.
    ==3407== Using Valgrind-3.10.0 and LibVEX; rerun with -h for copyright info
    ==3407== Command: ./test1
    ==3407==
    0x53db068 = 1
    ==3407==
    ==3407== HEAP SUMMARY:
    ==3407==     in use at exit: 0 bytes in 0 blocks
    ==3407==   total heap usage: 1 allocs, 1 frees, 48 bytes allocated
    ==3407==
    ==3407== All heap blocks were freed -- no leaks are possible
    ==3407==
    ==3407== For counts of detected and suppressed errors, rerun with: -v
    ==3407== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
    ```
* Simple unique\_ptr with destructor:
    ```c
    #include <unistd.h>
    #include <fcntl.h>
    #include <csptr_smart_ptr.h>
    
    struct log_file {
        int fd;
        // ...
    };
    
    void cleanup_log_file(void *ptr, void *meta) {
        (void) meta;
        close(((struct log_file *) ptr)->fd);
    }
    
    int main(void) {
        smart struct log_file *log = unique_ptr(struct log_file, {
                .fd = open("/dev/null", O_WRONLY | O_APPEND),
                // ...
            }, cleanup_log_file);
    
        write(log->fd, "Hello", 5);
    
        // cleanup_log_file is called, then log is freed
        return 0;
    }
    ```
* Allocating a smart array and printing its contents before destruction:
    ```c
    #include <stdio.h>
    #include <csptr_smart_ptr.h>
    
    void print_int(void *ptr, void *meta) {
        (void) meta;
        // ptr points to the current element
        // meta points to the array metadata (global to the array), if any.
        printf("%d\n", *(int*) ptr);
    }
    
    int main(void) {
        // Destructors for array types are run on every element of the
        // array before destruction.
        smart int *ints = unique_ptr(int[5], {5, 4, 3, 2, 1}, print_int);
        // ints == {5, 4, 3, 2, 1}
    
        // Smart arrays are length-aware
        for (size_t i = 0; i < array_length(ints); ++i) {
            ints[i] = i + 1;
        }
        // ints == {1, 2, 3, 4, 5}
    
        return 0;
    }
    ```

## More examples

* Using a different memory allocator (although most will replace malloc/free):
    ```c
    #include <csptr_smart_ptr.h>
    
    void *some_allocator(size_t);
    void some_deallocator(void *);
    
    int main(void) {
        smalloc_allocator = (s_allocator) {some_allocator, some_deallocator};
        // ...
        return 0;
    }
    ```

* Automatic cleanup on error cases:
    ```c
    #include <unistd.h>
    #include <fcntl.h>
    #include <csptr_smart_ptr.h>
    
    struct log_file {
        int fd;
        // ...
    };
    
    static void close_log(void *ptr, void *meta) {
        (void) meta;
        struct log_file *log = ptr;
        if (log->fd != -1)
            close(log->fd);
    }
    
    struct log_file *open_log(const char *path) {
        smart struct log_file *log = shared_ptr(struct log_file, {0}, close_log);
        if (!log) // failure to allocate
            return NULL; // nothing happens, destructor is not called
    
        log->fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (log->fd == -1) // failure to open
            return NULL; // log gets destroyed, file descriptor is not closed since fd == -1.
    
        return sref(log); // a new reference on log is returned, it does not get destoyed
    }
    
    int main(void) {
        smart struct log_file *log = open_log("/dev/null");
        // ...
        return 0; // file descriptor is closed, log is freed
    }
    ```
* Using named parameters:
    ```c
    #include <csptr_smart_ptr.h>
    
    void nothing(void *ptr, void *meta) {}
    
    int main(void) {
        struct { int a; } m = { 1 };
    
        smart int *i = unique_ptr(int,
                .dtor = nothing,
                .value = 42,
                .meta = { &m, sizeof (m) }
            );
    
        return 0;
    }
    ```

## FAQ

**Q. Why didn't you use C++ you moron ?**  
A. Because when I first started this, I was working on a C project.
   Also, because it's fun.

**Q. Can I use this on a serious project ?**  
A. Yes, but as this project has not been widely used, there might be
   some bugs. Beware!

**Q. How did you make this ?**  
A. Here's a [link to my blog post](http://snaipe.me/c/c-smart-pointers/) on the matter.

## My demos

- realloc：

  ```c
  #include <stdio.h>
  #include "csptr_smart_ptr.h"
  
  typedef struct {
          int *a;
          int b;
  } xxx_t;
  
  void dtor_xxx(void *param, void *meta){
          xxx_t *x = (xxx_t*)param;
          if (x->a) {
                  smart_free(&(x->a));
          }
  }
  
  int main(void) {
          smart int *one_int = shared_ptr(int, 666);
          int *second_int = one_int;
          print_smart_ptr_layout(one_int);
          printf("%p = %d\n", one_int, *one_int);
          smart xxx_t* x = shared_ptr(xxx_t, {sref(one_int), 2}, dtor_xxx);
          print_smart_ptr_layout(x);
          print_smart_ptr_layout(one_int);
          *one_int = 888;
          printf("%p = %d\n", one_int, *one_int);
          smart int *new_int = smart_realloc(&one_int, 40);
          *(new_int+8) = 1024;
          printf("origin one_int points to %p\n", one_int);
          printf("exit\n");
      return 0;
  }
  ```

  `gcc -g demo1.c csptr_smart_ptr.c -rdynamic`：

  ```
  # ./a.out 
  [smartptr.smalloc] malloc memory at 0x17d8048 with aligned size 8(B)/origin request size is 4(B), total with meta is 64B
  [smartptr] [0x17d8010·元数据(SHARED-1/48B) | 0x17d8040·元数据大小(48/8B) | 0x17d8048·用户数据(4B) | 0x17d804c·用户数据对齐填充(4B)] => total 64B
  0x17d8048 = 666
  [smartptr.smalloc] malloc memory at 0x17d8098 with aligned size 16(B)/origin request size is 16(B), total with meta is 72B
  [smartptr] [0x17d8060·元数据(SHARED-1/48B) | 0x17d8090·元数据大小(48/8B) | 0x17d8098·用户数据(16B)] => total 72B
  [smartptr] [0x17d8010·元数据(SHARED-2/48B) | 0x17d8040·元数据大小(48/8B) | 0x17d8048·用户数据(4B) | 0x17d804c·用户数据对齐填充(4B)] => total 64B
  0x17d8048 = 888
  [smartptr.realloc] realloc from 0x17d8048 to 0x17d80e8 success, total with meta is 96B, the ref count is reset to 1, ptr for smart_realloc(&ptr) has been reset to NULL, 
  warning: all old ptrs that point to the old shared obj(0x17d8048) need to be reset as NULL to avoid `Dangling Pointer`, new obj: 
  [smartptr] [0x17d80b0·元数据(SHARED-1/48B) | 0x17d80e0·元数据大小(48/8B) | 0x17d80e8·用户数据(40B)] => total 96B
  origin one_int points to (nil)
  exit
  [smartptr.sfree] free memory 0x17d80e8 success
  [smartptr.sfree] call dtor(0x4012b0) func before free memory 0x17d8098
  [smartptr.error] get meta from 0x17d8048 failed for <invalid_meta:1,freed_heap:0,bad_userdata_ptr:0>, please check code
  ./a.out(sfree+0x12a) [0x4037ce]
  ./a.out(smart_free+0x2a) [0x401ac3]
  ./a.out(dtor_xxx+0x30) [0x4012e0]
  ./a.out(sfree+0x702) [0x403da6]
  ./a.out(smart_free+0x2a) [0x401ac3]
  ./a.out(main+0x272) [0x401554]
  /lib64/libc.so.6(__libc_start_main+0xf5) [0x7f0255f29555]
  ./a.out() [0x400f19]
  [smartptr.sfree] free memory 0x17d8098 success
  [smartptr] all malloc memory(2-obj) has been released safely!
  ```

- ...

