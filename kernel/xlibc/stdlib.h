#pragma once

#include <xlibc/xstddef.h>

void* malloc(size_t size);
void free(void* ptr);

// --- FROM MUSL --- //
int abs (int);
long labs (long);
long long llabs (long long);
int atoi(const char *s);
double atof(const char *s);
// --- FROM MUSL --- //