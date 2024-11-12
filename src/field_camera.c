#include "global.h"
#include "berry.h"
#include "bike.h"
#include "field_camera.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "event_object_movement.h"
#include "gpu_regs.h"
#include "menu.h"
#include "overworld.h"
#include "rotating_gate.h"
#include "sprite.h"
#include "text.h"

EWRAM_DATA bool8 gUnusedBikeCameraAheadPanback = FALSE;

struct FieldCameraOffset
{
    u16 xPixelOffset;
    u16 yPixelOffset;
    u8 xTileOffset;
    u8 yTileOffset;
    bool8 copyBGToVRAM;
};

static void RedrawMapSliceNorth(struct FieldCameraOffset *, const struct MapLayout *, int, int);
static void RedrawMapSliceSouth(struct FieldCameraOffset *, const struct MapLayout *, int, int);
static void RedrawMapSliceEast(struct FieldCameraOffset *, const struct MapLayout *, int, int);
static void RedrawMapSliceWest(struct FieldCameraOffset *, const struct MapLayout *, int, int);
static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *, s32, s32);
static void DrawWholeMapViewInternal(int, int, const struct MapLayout *);
static void DrawMetatileAt(const struct MapLayout *, u16, int, int);
static void DrawMetatileHalfLeft(const struct MapLayout *, u16, int, int);
static void DrawMetatileHalfRight(const struct MapLayout *, u16, int, int);
static void DrawMetatile(s32, const u16 *, u16);
static void CameraPanningCB_PanAhead(void);
static void OffsetCameraPosition(int x, int y);

static struct FieldCameraOffset sFieldCameraOffset;
static s16 sHorizontalCameraPan;
static s16 sVerticalCameraPan;
static bool8 sBikeCameraPanFlag;
static void (*sFieldCameraPanningCallback)(void);

struct Coords16 gCameraPosition;
struct CameraObject gFieldCamera;
u16 gTotalCameraPixelOffsetY;
u16 gTotalCameraPixelOffsetX;

static void UpdateCameraTileOffset(void)
{
    sFieldCameraOffset.xTileOffset = (COORDS_TO_GRID(sFieldCameraOffset.xPixelOffset) * 2) % 32;
    sFieldCameraOffset.yTileOffset = (COORDS_TO_GRID(sFieldCameraOffset.yPixelOffset) * 2) % 32;
}

static void GetCameraMinMax(int *minX, int *minY, int *maxX, int *maxY)
{
    *minX = GRID_TO_COORDS(4);
    *minY = GRID_TO_COORDS(4);
    *maxX = GRID_TO_COORDS(gMapHeader.mapLayout->width - 4);
    *maxY = GRID_TO_COORDS(gMapHeader.mapLayout->height - 4);
}

void SetCameraPosition(int x, int y)
{
#if 0
    int minX;
    int minY;
    int maxX;
    int maxY;
#endif

    gFieldCamera.x = x;
    gFieldCamera.y = y;

#if 0
    GetCameraMinMax(&minX, &minY, &maxX, &maxY);

    if (x < minX)
    {
        if (!IsConnectionAvailable(CONNECTION_WEST))
            x = minX;
    }
    else if (x > maxX)
    {
        if (!IsConnectionAvailable(CONNECTION_EAST))
            x = maxX;
    }

    if (y < minY)
    {
        if (!IsConnectionAvailable(CONNECTION_NORTH))
            y = minY;
    }
    else if (y > maxY)
    {
        if (!IsConnectionAvailable(CONNECTION_SOUTH))
            y = maxY;
    }
#endif

    gCameraPosition.x = x;
    gCameraPosition.y = y;

    sFieldCameraOffset.xPixelOffset = x;
    sFieldCameraOffset.yPixelOffset = y;
    UpdateCameraTileOffset();
}

void OffsetCameraPosition(int x, int y)
{
    gCameraPosition.x += x;
    gCameraPosition.y += y;

    gTotalCameraPixelOffsetX = -(gCameraPosition.x >> OBJECT_EVENT_FRAC_SHIFT);
    gTotalCameraPixelOffsetY = -(gCameraPosition.y >> OBJECT_EVENT_FRAC_SHIFT);

    if (x != 0 || y != 0)
    {
        sFieldCameraOffset.xPixelOffset += x;
        sFieldCameraOffset.yPixelOffset += y;
        UpdateCameraTileOffset();
    }
}

void OffsetCameraPositionForTransition(int x, int y)
{
    gCameraPosition.x -= x;
    gCameraPosition.y -= y;
    gFieldCamera.x -= x;
    gFieldCamera.y -= y;
    gSprites[gFieldCamera.spriteId].x -= x;
    gSprites[gFieldCamera.spriteId].y -= y;
}

void ResetFieldCamera(void)
{
    SetCameraPosition(gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
    sFieldCameraOffset.copyBGToVRAM = TRUE;
}

void FieldUpdateBgTilemapScroll(void)
{
    u32 r4, r5;
    r5 = (sFieldCameraOffset.xPixelOffset >> OBJECT_EVENT_FRAC_SHIFT) + sHorizontalCameraPan - 8;
    r4 = (sFieldCameraOffset.yPixelOffset >> OBJECT_EVENT_FRAC_SHIFT) + sVerticalCameraPan;

    SetGpuReg(REG_OFFSET_BG1HOFS, r5);
    SetGpuReg(REG_OFFSET_BG1VOFS, r4);
    SetGpuReg(REG_OFFSET_BG2HOFS, r5);
    SetGpuReg(REG_OFFSET_BG2VOFS, r4);
    SetGpuReg(REG_OFFSET_BG3HOFS, r5);
    SetGpuReg(REG_OFFSET_BG3VOFS, r4);
}

void GetCameraOffsetWithPan(s16 *x, s16 *y)
{
    *x = (sFieldCameraOffset.xPixelOffset >> OBJECT_EVENT_FRAC_SHIFT) + sHorizontalCameraPan - 8;
    *y = (sFieldCameraOffset.yPixelOffset >> OBJECT_EVENT_FRAC_SHIFT) + sVerticalCameraPan;
}

void DrawWholeMapView(void)
{
    DrawWholeMapViewInternal(COORDS_TO_GRID(gCameraPosition.x), COORDS_TO_GRID(gCameraPosition.y), gMapHeader.mapLayout);
    sFieldCameraOffset.copyBGToVRAM = TRUE;
}

static void DrawWholeMapViewInternal(int x, int y, const struct MapLayout *mapLayout)
{
    u8 i;
    u8 j;
    u32 r6;
    u8 temp;

    for (i = 0; i < 32; i += 2)
    {
        temp = sFieldCameraOffset.yTileOffset + i;
        if (temp >= 32)
            temp -= 32;
        r6 = temp * 32;
        for (j = 0; j < 32; j += 2)
        {
            temp = sFieldCameraOffset.xTileOffset + j;
            if (temp >= 32)
                temp -= 32;
            DrawMetatileAt(mapLayout, r6 + temp, x + j / 2, y + i / 2);
        }
    }

    RedrawMapSliceWest(&sFieldCameraOffset, mapLayout, x, y);
    RedrawMapSliceEast(&sFieldCameraOffset, mapLayout, x, y);
}

static void RedrawMapSlicesForCameraUpdate(struct FieldCameraOffset *cameraOffset, int x, int y)
{
    const struct MapLayout *mapLayout = gMapHeader.mapLayout;
    int viewX = COORDS_TO_GRID(gCameraPosition.x);
    int viewY = COORDS_TO_GRID(gCameraPosition.y);

    if (x > 0)
        RedrawMapSliceWest(cameraOffset, mapLayout, viewX, viewY);
    else if (x < 0)
        RedrawMapSliceEast(cameraOffset, mapLayout, viewX, viewY);
    if (y > 0)
        RedrawMapSliceNorth(cameraOffset, mapLayout, viewX, viewY);
    else if (y < 0)
        RedrawMapSliceSouth(cameraOffset, mapLayout, viewX, viewY);

    cameraOffset->copyBGToVRAM = TRUE;
}

static void RedrawMapSliceNorth(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout, int viewX, int viewY)
{
    u8 i;
    u8 temp;
    s32 temp2;
    u32 r7;

    temp = cameraOffset->yTileOffset + 28;
    if (temp >= 32)
        temp -= 32;
    r7 = temp * 32;
    for (i = 0; i < 32; i += 2)
    {
        temp = cameraOffset->xTileOffset + i;
        if (temp >= 32)
            temp -= 32;
        DrawMetatileAt(mapLayout, r7 + temp, viewX + i / 2, viewY + 14);
    }

    temp2 = r7 + cameraOffset->xTileOffset - 2;
    if (temp2 < 0)
        temp2 += 32;
    DrawMetatileHalfRight(mapLayout, temp2, viewX - 1, viewY + 14);
}

static void RedrawMapSliceSouth(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout, int viewX, int viewY)
{
    u8 i;
    u8 temp;
    s32 temp2;
    u32 r7 = cameraOffset->yTileOffset * 32;

    for (i = 0; i < 32; i += 2)
    {
        temp = cameraOffset->xTileOffset + i;
        if (temp >= 32)
            temp -= 32;
        DrawMetatileAt(mapLayout, r7 + temp, viewX + i / 2, viewY);
    }

    temp2 = r7 + cameraOffset->xTileOffset - 2;
    if (temp2 < 0)
        temp2 += 32;
    DrawMetatileHalfRight(mapLayout, temp2, viewX - 1, viewY);
}

static void RedrawMapSliceEast(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout, int viewX, int viewY)
{
    u8 i;
    u8 temp;
    u32 x2 = cameraOffset->xTileOffset;
    s32 x1 = x2 - 2;

    if (x1 < 0)
        x1 += 32;

    for (i = 0; i < 32; i += 2)
    {
        temp = cameraOffset->yTileOffset + i;
        if (temp >= 32)
            temp -= 32;
        DrawMetatileHalfRight(mapLayout, temp * 32 + x1, viewX - 1, viewY + i / 2);
        DrawMetatileAt(mapLayout, temp * 32 + x2, viewX, viewY + i / 2);
    }
}

static void RedrawMapSliceWest(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout, int viewX, int viewY)
{
    u8 i;
    u8 temp;
    u8 x1 = cameraOffset->xTileOffset + 28;
    u8 x2 = x1 + 2;

    if (x1 >= 32)
        x1 -= 32;
    if (x2 >= 32)
        x2 -= 32;
    for (i = 0; i < 32; i += 2)
    {
        temp = cameraOffset->yTileOffset + i;
        if (temp >= 32)
            temp -= 32;
        DrawMetatileAt(mapLayout, temp * 32 + x1, viewX + 14, viewY + i / 2);
        DrawMetatileHalfLeft(mapLayout, temp * 32 + x2, viewX + 15, viewY + i / 2);
    }
}

void CurrentMapDrawMetatileAt(int x, int y)
{
    int offset = MapPosToBgTilemapOffset(&sFieldCameraOffset, x, y);

    if (offset >= 0)
    {
        DrawMetatileAt(gMapHeader.mapLayout, offset, x, y);
        sFieldCameraOffset.copyBGToVRAM = TRUE;
    }
}

void DrawDoorMetatileAt(int x, int y, u16 *tiles)
{
    int offset = MapPosToBgTilemapOffset(&sFieldCameraOffset, x, y);

    if (offset >= 0)
    {
        DrawMetatile(METATILE_LAYER_TYPE_COVERED, tiles, offset);
        sFieldCameraOffset.copyBGToVRAM = TRUE;
    }
}

static const u16 *GetMetatile(const struct MapLayout *mapLayout, int x, int y)
{
    u16 metatileId = MapGridGetMetatileIdAt(x, y);
    const u16 *metatiles;

    if (metatileId > NUM_METATILES_TOTAL)
        metatileId = 0;
    if (metatileId < NUM_METATILES_IN_PRIMARY)
        metatiles = mapLayout->primaryTileset->metatiles;
    else
    {
        metatiles = mapLayout->secondaryTileset->metatiles;
        metatileId -= NUM_METATILES_IN_PRIMARY;
    }
    return metatiles + metatileId * NUM_TILES_PER_METATILE;
}

static void DrawMetatileAt(const struct MapLayout *mapLayout, u16 offset, int x, int y)
{
    DrawMetatile(MapGridGetMetatileLayerTypeAt(x, y), GetMetatile(mapLayout, x, y), offset);
}

static void DrawMetatile(s32 metatileLayerType, const u16 *tiles, u16 offset)
{
    switch (metatileLayerType)
    {
    case METATILE_LAYER_TYPE_SPLIT:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw transparent tiles to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 1] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = 0;

        // Draw metatile's top layer to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    case METATILE_LAYER_TYPE_COVERED:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg2[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[7];

        // Draw transparent tiles to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 1] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = 0;
        break;
    case METATILE_LAYER_TYPE_NORMAL:
        // Draw garbage to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 1] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = 0x3014;

        // Draw metatile's bottom layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg2[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the top background layer, which covers object event sprites.
        gOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    }
    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    ScheduleBgCopyTilemapToVram(3);
}

static void DrawMetatileHalfLeft(const struct MapLayout *mapLayout, u16 offset, int x, int y)
{
    const u16 *tiles = GetMetatile(mapLayout, x, y);
    switch (MapGridGetMetatileLayerTypeAt(x, y))
    {
    case METATILE_LAYER_TYPE_SPLIT:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];

        // Draw transparent tiles to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = 0;

        // Draw metatile's top layer to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        break;
    case METATILE_LAYER_TYPE_COVERED:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];

        // Draw metatile's top layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[6];

        // Draw transparent tiles to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = 0;
        break;
    case METATILE_LAYER_TYPE_NORMAL:
        // Draw garbage to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = 0x3014;

        // Draw metatile's bottom layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[2];

        // Draw metatile's top layer to the top background layer, which covers object event sprites.
        gOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        break;
    }
    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    ScheduleBgCopyTilemapToVram(3);
}

static void DrawMetatileHalfRight(const struct MapLayout *mapLayout, u16 offset, int x, int y)
{
    const u16 *tiles = GetMetatile(mapLayout, x, y);
    switch (MapGridGetMetatileLayerTypeAt(x, y))
    {
    case METATILE_LAYER_TYPE_SPLIT:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw transparent tiles to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset + 1] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = 0;

        // Draw metatile's top layer to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    case METATILE_LAYER_TYPE_COVERED:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[7];

        // Draw transparent tiles to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset + 1] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = 0;
        break;
    case METATILE_LAYER_TYPE_NORMAL:
        // Draw garbage to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset + 1] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = 0x3014;

        // Draw metatile's bottom layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the top background layer, which covers object event sprites.
        gOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    }
    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    ScheduleBgCopyTilemapToVram(3);
}

static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *cameraOffset, s32 x, s32 y)
{
    x -= COORDS_TO_GRID(gCameraPosition.x);
    x *= 2;
    if (x >= 32 || x < 0)
        return -1;
    x = x + cameraOffset->xTileOffset;
    if (x >= 32)
        x -= 32;

    y = (y - COORDS_TO_GRID(gCameraPosition.y)) * 2;
    if (y >= 32 || y < 0)
        return -1;
    y = y + cameraOffset->yTileOffset;
    if (y >= 32)
        y -= 32;

    return y * 32 + x;
}

static void CameraUpdateCallback(struct CameraObject *fieldCamera)
{
    if (fieldCamera->spriteId != 0)
    {
        fieldCamera->movementSpeedX = gSprites[fieldCamera->spriteId].data[2];
        fieldCamera->movementSpeedY = gSprites[fieldCamera->spriteId].data[3];
    }
}

void ResetCameraUpdateInfo(void)
{
    gFieldCamera.movementSpeedX = 0;
    gFieldCamera.movementSpeedY = 0;
    gFieldCamera.spriteId = 0;
    gFieldCamera.callback = NULL;
}

u32 InitCameraUpdateCallback(u8 trackedObjectEventId)
{
    if (gFieldCamera.spriteId != 0)
        DestroySprite(&gSprites[gFieldCamera.spriteId]);
    gFieldCamera.spriteId = AddCameraObject(trackedObjectEventId);
    gFieldCamera.callback = CameraUpdateCallback;
    return 0;
}

void CameraUpdate(void)
{
    int deltaX;
    int deltaY;
    int movementSpeedX;
    int movementSpeedY;
#if 0
    int minX;
    int minY;
    int maxX;
    int maxY;
#endif
    int newPosX;
    int newPosY;

    if (gFieldCamera.callback != NULL)
        gFieldCamera.callback(&gFieldCamera);
    movementSpeedX = gFieldCamera.movementSpeedX;
    movementSpeedY = gFieldCamera.movementSpeedY;

    gFieldCamera.x += movementSpeedX;
    gFieldCamera.y += movementSpeedY;

    newPosX = gFieldCamera.x;
    newPosY = gFieldCamera.y;

#if 0
    GetCameraMinMax(&minX, &minY, &maxX, &maxY);

    if (gFieldCamera.x < minX)
    {
        if (!IsConnectionAvailable(CONNECTION_WEST))
        {
            newPosX = minX;
        }
    }
    else if (gFieldCamera.x > maxX)
    {
        if (!IsConnectionAvailable(CONNECTION_EAST))
        {
            newPosX = maxX;
        }
    }

    if (gFieldCamera.y < minY)
    {
        if (!IsConnectionAvailable(CONNECTION_NORTH))
        {
            newPosY = minY;
        }
    }
    else if (gFieldCamera.y > maxY)
    {
        if (!IsConnectionAvailable(CONNECTION_SOUTH))
        {
            newPosY = maxY;
        }
    }
#endif

    deltaX = newPosX - gCameraPosition.x;
    deltaY = newPosY - gCameraPosition.y;

    if (movementSpeedX != 0 || movementSpeedY != 0)
    {
        CameraMove(movementSpeedX, movementSpeedY);
        OffsetCameraPosition(deltaX, deltaY);
        UpdateObjectEventsForCameraUpdate();
        SetBerryTreesSeen();
        RotatingGatePuzzleCameraUpdate();
        RedrawMapSlicesForCameraUpdate(&sFieldCameraOffset, deltaX, deltaY);
    }
}

void SetCameraPanningCallback(void (*callback)(void))
{
    sFieldCameraPanningCallback = callback;
}

void SetCameraPanning(s16 horizontal, s16 vertical)
{
    sHorizontalCameraPan = horizontal;
    sVerticalCameraPan = vertical + 32;
}

void InstallCameraPanAheadCallback(void)
{
    sFieldCameraPanningCallback = CameraPanningCB_PanAhead;
    sBikeCameraPanFlag = FALSE;
    sHorizontalCameraPan = 0;
    sVerticalCameraPan = 32;
}

void UpdateCameraPanning(void)
{
    if (sFieldCameraPanningCallback != NULL)
        sFieldCameraPanningCallback();

    // Update sprite offset of overworld objects
    gSpriteCoordOffsetX = gTotalCameraPixelOffsetX - sHorizontalCameraPan;
    gSpriteCoordOffsetY = gTotalCameraPixelOffsetY - sVerticalCameraPan - 8;
}

static void CameraPanningCB_PanAhead(void)
{
    u8 var;

    if (gUnusedBikeCameraAheadPanback == FALSE)
    {
        InstallCameraPanAheadCallback();
    }
    else
    {
        // this code is never reached
        if (gPlayerAvatar.tileTransitionState == T_TILE_TRANSITION)
        {
            sBikeCameraPanFlag ^= 1;
            if (sBikeCameraPanFlag == FALSE)
                return;
        }
        else
        {
            sBikeCameraPanFlag = FALSE;
        }

        var = GetPlayerMovementDirection();
        if (var == 2)
        {
            if (sVerticalCameraPan > -8)
                sVerticalCameraPan -= 2;
        }
        else if (var == 1)
        {
            if (sVerticalCameraPan < 72)
                sVerticalCameraPan += 2;
        }
        else if (sVerticalCameraPan < 32)
        {
            sVerticalCameraPan += 2;
        }
        else if (sVerticalCameraPan > 32)
        {
            sVerticalCameraPan -= 2;
        }
    }
}
