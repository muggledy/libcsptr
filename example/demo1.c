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