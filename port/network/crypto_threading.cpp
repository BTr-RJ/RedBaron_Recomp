#include <mbedtls/threading.h>
#include <mutex>
namespace {
void Init(mbedtls_threading_mutex_t* m) {
  if(m){InitializeSRWLock(&m->lock);m->initialized=1;}
}
void Free(mbedtls_threading_mutex_t* m){if(m)m->initialized=0;}
int Lock(mbedtls_threading_mutex_t* m) {
  if(!m || !m->initialized)return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
  AcquireSRWLockExclusive(&m->lock);return 0;
}
int Unlock(mbedtls_threading_mutex_t* m) {
  if(!m || !m->initialized)return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
  ReleaseSRWLockExclusive(&m->lock);return 0;
}
}
extern "C" __declspec(dllexport) void RedBaronInitCryptoThreading() {
  static std::once_flag once;
  std::call_once(once,[]{mbedtls_threading_set_alt(Init,Free,Lock,Unlock);});
}
