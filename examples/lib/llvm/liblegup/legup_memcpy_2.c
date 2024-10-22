#include "legup/intrinsics.h"

void legup_memcpy_2(uint8_t *d, const uint8_t *s, uint32_t n) {
    uint16_t *dt = (uint16_t *)d;
    const uint16_t *st = (const uint16_t *)s;
    n >>= 1;
    while (n--)
        *dt++ = *st++;
}
