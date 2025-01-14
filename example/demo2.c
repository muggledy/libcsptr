#include <stdio.h>
#include "csptr_smart_ptr.h"

int main(void) {
        smart int *one_int = unique_ptr(int, 666);
        int *second_int = one_int;
        print_smart_ptr_layout(one_int);
        printf("%p = %d\n", one_int, *one_int);
        *one_int = 888;
        printf("%p = %d\n", one_int, *one_int);
        smart int *new_int = smart_realloc(&one_int, 400);
        *(new_int+8) = 1024;
        printf("origin one_int points to %p\n", one_int);
        printf("exit\n");
    return 0;
}