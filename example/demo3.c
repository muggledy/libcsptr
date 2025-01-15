#include <stdio.h>
#include "csptr_smart_ptr.h"

void *create_one_int() {
        smart int *one_int = unique_ptr(int, 666);
        printf("one_int(%p):%d\n", one_int, *one_int);
        return smove(one_int); //如果去掉smove，那么函数返回，one_int对应的堆就立即被释放了，可以试试看
}

int main(void) {
        smart int *x = create_one_int();
        print_smart_ptr_layout(x);
        printf("x(%p) = %d\n", x, *x);
        printf("exit\n");
    return 0;
}