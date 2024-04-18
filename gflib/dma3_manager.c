#include "global.h"
#include "dma3.h"

// #define DMA3_DEBUG

s16 RequestDma3Copy(const void *src, void *dest, size_t size, u8 mode)
{
#ifdef DMA3_DEBUG
    printf("RequestDma3Copy: (src: %p, dest: %p, size: %u)\n", src, dest, size);
#endif

    // Just copy it. Who cares?
    (void)mode;
    memcpy(dest, src, size);
    return 1;
}

s16 RequestDma3Fill(s32 value, void *dest, size_t size, u8 mode)
{
#ifdef DMA3_DEBUG
    printf("RequestDma3Fill: (value: %p, dest: %u, size: %u)\n", value, dest, size);
#endif

    // Just fill it. Who cares?
    (void)mode;
    memset(dest, value, size);
    return 1;
}
