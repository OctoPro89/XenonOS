#include <xlibc/stdio.h>
#include <xlibc/stdlib.h>

int main(void) {
    /*
    printf("Starting FS test!\n");

    printf("Opening testlongfilename.txt in read-binary mode.\n");
    FILE* file = fopen("testlongfilename.txt", "rb");
    if (!file) {
        printf("Failed to open file!\n");
        return -1;
    }

    printf("Successfully opened testlongfilename.txt.\n");

    printf("Seeking to end of file.");
    if (fseek(file, 0, SEEK_END) < 0) {
        printf("Failed to seek to end of file (fseek(file, 0, SEEK_END))!\n");
        return -1;
    }
    printf("Successfully seeked to end of file.\n");

    printf("Getting file size.\n");
    long size = ftell(file);
    if (size < 0) {
        printf("Failed to get file size!\n");
        fclose(file);
        return -1;
    }

    printf("Got file size: %ld bytes\n", size);

    printf("Seeking to start of file.");
    if (fseek(file, 0, SEEK_SET) < 0) {
        fclose(file);
        printf("Failed to seek to start of file (fseek(file, 0, SEEK_SET))!\n");
        return -1;
    }

    printf("Successfully seeked to start of file.\n");

    printf("Allocating 26 byte buffer for reading data\n");
    unsigned char* buffer = malloc(26);
    if (!buffer) {
        fclose(file);
        printf("Failed to allocate 26 byte buffer!\n");
        return -1;
    }

    printf("Allocation succeeded.\n");

    printf("Reading 10 bytes from file.\n");
    if (fread(buffer, 1, 10, file) == 0) {
        free(buffer);
        fclose(file);
        printf("Failed to read 10 bytes from file!\n");
        return -1;
    }

    printf("Successfully read 10 bytes from file:\n");

    for (int i = 0; i < 10; ++i) {
        putc((char)buffer[i]);
    }

    printf("\n");

    printf("Reading 16 bytes from file.\n");
    if (fread(buffer, 1, 16, file) == 0) {
        free(buffer);
        fclose(file);
        printf("Failed to read 16 bytes from file!\n");
        return -1;
    }

    printf("Successfully read 16 bytes from file:\n");

    printf("Seeking back 20 bytes.\n");
    if (fseek(file, -20, SEEK_CUR) < 0) {
        printf("Failed to seek back 20 bytes (fseek(file, -20, SEEK_CUR))!\n");
        return -1;
    }

    printf("Reading 16 bytes from file.\n");
    if (fread(buffer, 1, 16, file) == 0) {
        free(buffer);
        fclose(file);
        printf("Failed to read 16 bytes from file!\n");
        return -1;
    }

    printf("Successfully read 16 bytes from file:\n");

    free(buffer);
    fclose(file);

    printf("FS test successful.\n");
    */

    printf("Starting FS test!\n");

    printf("Opening doom1.wad in read-binary mode.\n");
    FILE* file = fopen("doom1.wad", "rb");
    if (!file) {
        printf("Failed to open file!\n");
        return -1;
    }

    printf("Successfully opened doom1.wad.\n");

    printf("Seeking to end of file.");
    if (fseek(file, 0, SEEK_END) < 0) {
        printf("Failed to seek to end of file (fseek(file, 0, SEEK_END))!\n");
        return -1;
    }
    printf("Successfully seeked to end of file.\n");

    printf("Getting file size.\n");
    long size = ftell(file);
    if (size < 0) {
        printf("Failed to get file size!\n");
        fclose(file);
        return -1;
    }

    printf("Got file size: %ld bytes\n", size);

    printf("Seeking to start of file.");
    if (fseek(file, 0, SEEK_SET) < 0) {
        fclose(file);
        printf("Failed to seek to start of file (fseek(file, 0, SEEK_SET))!\n");
        return -1;
    }

    printf("Successfully seeked to start of file.\n");

    printf("Allocating byte buffer for reading data\n");
    unsigned char* buffer = malloc(size);
    if (!buffer) {
        fclose(file);
        printf("Failed to allocate byte buffer!\n");
        return -1;
    }

    printf("Allocation succeeded.\n");

    printf("Reading bytes from file.\n");
    if (fread(buffer, 1, size, file) == 0) {
        free(buffer);
        fclose(file);
        printf("Failed to read SIZE bytes from file!\n");
        return -1;
    }

    printf("Successfully read bytes from file:\n");

    for (int i = 0; i < 10; ++i) {
        putc((char)buffer[i]);
    }

    printf("\n");

    free(buffer);
    fclose(file);

    printf("FS test successful.\n");
    return 0;
}