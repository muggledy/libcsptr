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
        /*test: gcc -g demo.c csptr_smart_ptr.c -rdynamic*/
        //smart_free(&one_int); //call smart_free() to free one unique/shared obj manually. free memory failed herein for ref count > 0
        //smart_free(&one_int); //one_int is NULL, do nothing
        //smart_free(&second_int); //ref decrease to 0, the shared obj one_int's memory can be freed normally, but x->a still points to one_int's memory, smart_free x->a in dtor_xxx means double free! but we can detect it by our `smartptr`, won't cause crash problem
        /*end test*/
        printf("exit\n");
    return 0;
}