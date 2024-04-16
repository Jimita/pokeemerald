#include "global.h"
#include "graphics.h"

// Duplicate of sBerryFixGraphics in berry_fix_program.c
static const struct {
    const u32 *gfx;
    const u32 *tilemap;
    const u16 *pltt;
} sBerryFixGraphics[] = {
    {
        gBerryFixGbaConnect_Gfx,
        gBerryFixGbaConnect_Tilemap,
        gBerryFixGbaConnect_Pal
    }, {
        gBerryFixGameboyLogo_Gfx,
        gBerryFixGameboyLogo_Tilemap,
        gBerryFixGameboyLogo_Pal
    }, {
        gBerryFixGbaTransfer_Gfx,
        gBerryFixGbaTransfer_Tilemap,
        gBerryFixGbaTransfer_Pal
    }, {
        gBerryFixGbaTransferHighlight_Gfx,
        gBerryFixGbaTransferHighlight_Tilemap,
        gBerryFixGbaTransferHighlight_Pal
    }, {
        gBerryFixGbaTransferError_Gfx,
        gBerryFixGbaTransferError_Tilemap,
        gBerryFixGbaTransferError_Pal
    }, {
        gBerryFixWindow_Gfx,
        gBerryFixWindow_Tilemap,
        gBerryFixWindow_Pal
    }
};

// See berry_fix_program.c
static void UNUSED LoadBerryFixGraphics(u32 idx)
{
#if 0
    ClearGpuBackgroundState(0);
    SetGpuState(GPU_STATE_DISPCNT, 0);
    SetGpuBackgroundX(0, 0);
    SetGpuBackgroundY(0, 0);
    SetGpuState(GPU_STATE_BLDCNT, 0);
    LZ77UnCompVram(sBerryFixGraphics[idx].gfx, (void *)BG_CHAR_ADDR(0));
    LZ77UnCompVram(sBerryFixGraphics[idx].tilemap, (void *)BG_SCREEN_ADDR(31));
    CpuCopy16(sBerryFixGraphics[idx].pltt, (void *)BG_PLTT, BG_PLTT_SIZE);
    SetGpuBackgroundScreenBaseBlock(0, 31);
    SetGpuState(GPU_STATE_DISPCNT, DISPCNT_BG0_ON);
#endif
}
