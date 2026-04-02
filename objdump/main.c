#include <stdlib.h>
#include "my_print.h"
#include <stdio.h>

void hello_from_main() {
    printf("hello from main\n");
}
int main() {
    my_print();
    hello_from_main();
    return EXIT_SUCCESS;
}
