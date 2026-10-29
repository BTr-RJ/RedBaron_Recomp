#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
// Mbed TLS alternative mutex type; used only by the isolated networking build.
typedef struct mbedtls_threading_mutex_t {
  SRWLOCK lock;
  int initialized;
} mbedtls_threading_mutex_t;
