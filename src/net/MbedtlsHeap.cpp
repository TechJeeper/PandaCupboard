#include <cstddef>
#include <esp_heap_caps.h>

// mbedTLS is compiled to allocate SSL record buffers from internal RAM
// (16KB in + 16KB out). The UI leaves ~20KB free, so Bambu TLS fails with
// "SSL - Memory allocation failed" even though PSRAM has megabytes free.
// Route large mbedTLS allocs into PSRAM.

extern "C" {

void *__wrap_esp_mbedtls_mem_calloc(size_t n, size_t size) {
    if (n == 0 || size == 0) return nullptr;
    const size_t bytes = n * size;
    void *p = nullptr;
    if (bytes >= 1024) {
        p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!p) {
        p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!p) {
        p = heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
    }
    return p;
}

void __wrap_esp_mbedtls_mem_free(void *ptr) {
    heap_caps_free(ptr);
}

}
