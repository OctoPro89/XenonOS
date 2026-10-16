#include <unistd.h>
#include <xlibc/stdlib.h>
#include <xlibc/stdio.h>

int main(void) {
    printf("Allocating 1 KiB of heap!\n");
    void* bytes = malloc(1024);
    if (!bytes) {
        printf("Allocation failed!\n");
        return -1;
    }
    free(bytes);

    printf("Allocating 1 MiB of heap!\n");
    bytes = malloc(1048576);
    if (!bytes) {
        printf("Allocation failed!\n");
        return -1;
    }
    free(bytes);

    printf("Allocating 2 MiB of heap!\n");
    bytes = malloc(1048576 * 2);
    if (!bytes) {
        printf("Allocation failed!\n");
        return -1;
    }
    free(bytes);

    printf("Allocating 3 MiB of heap!\n");
    bytes = malloc(1048576 * 3);
    if (!bytes) {
        printf("Allocation failed!\n");
        return -1;
    }
    free(bytes);

    printf("Allocating 4 MiB of heap!\n");
    bytes = malloc(1048576 * 4);
    if (!bytes) {
        printf("Allocation failed!\n");
        return -1;
    }
    free(bytes);

    printf("Allocating 5 MiB of heap!\n");
    bytes = malloc(1048576 * 5);
    if (!bytes) {
        printf("Allocation failed!\n");
        return -1;
    }
    free(bytes);

    printf("Heap test successful.\n");
    
    return 0;
}