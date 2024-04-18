#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

#include <SDL2/SDL.h>

#include "global.h"
#include "platform.h"
#include "rtc.h"
#include "gba/defines.h"
#include "gba/m4a_internal.h"
#include "gpu_main.h"
#include "m4a.h"
#include "main.h"
#include "cgb_audio.h"

u16 INTR_CHECK;
void *INTR_VECTOR;
unsigned char REG_BASE[0x400] __attribute__ ((aligned (4)));
unsigned char FLASH_BASE[131072] __attribute__ ((aligned (4)));
struct SoundInfo *SOUND_INFO_PTR;

struct scanlineData {
    uint16_t layers[NUM_BACKGROUNDS][DISPLAY_WIDTH];
    uint16_t spriteLayers[4][DISPLAY_WIDTH];
    uint16_t winMask[DISPLAY_WIDTH];
    //priority bookkeeping
    char bgtoprio[NUM_BACKGROUNDS]; //background to priority
    char prioritySortedBgs[4][4];
    char prioritySortedBgsCount[4];
};

struct bgPriority {
    char priority;
    char subPriority;
};

SDL_Window *sdlWindow;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;
bool speedUp = false;
unsigned int videoScale = 1;
bool videoScaleChanged = false;
bool recenterWindow = false;
bool isRunning = true;
bool paused = false;
double simTime = 0;
double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;
struct SiiRtcInfo internalClock;

#ifdef USE_THREAD
SDL_Thread *mainLoopThread;
SDL_sem *vBlankSemaphore;
SDL_atomic_t isFrameAvailable;
#endif

#define ALLOW_ANY_RESOLUTION

static s32 displayWidth = 0;
static s32 displayHeight = 0;

static s32 windowWidth = 0;
static s32 windowHeight = 0;

static bool8 runVCount = TRUE;
static bool8 runVBlank = TRUE;
static bool8 runHBlank = TRUE;
static bool8 layerEnabled[NUM_BACKGROUNDS + 1];

static FILE *sSaveFile = NULL;

#ifdef USE_THREAD
static int DoMain(void *param);
#endif

static void ProcessEvents(void);
static void RenderFrame(SDL_Texture *texture);

static void ReadSaveFile(char *path);
static void StoreSaveFile(void);
static void CloseSaveFile(void);

static void UpdateInternalClock(void);

static void RunFrame(void);

static void RunScanlineEffect(void);

static void AudioUpdate(void);

s32 DisplayWidth(void)
{
    return displayWidth;
}

s32 DisplayHeight(void)
{
    return displayHeight;
}

static bool8 SetResolution(s32 width, s32 height)
{
    if (width < BASE_DISPLAY_WIDTH)
        width = BASE_DISPLAY_WIDTH;
    else if (width > DISPLAY_WIDTH)
        width = DISPLAY_WIDTH;

    if (height < BASE_DISPLAY_HEIGHT)
        height = BASE_DISPLAY_HEIGHT;
    else if (height > DISPLAY_HEIGHT)
        height = DISPLAY_HEIGHT;

    if (displayWidth == width && displayHeight == height)
        return TRUE;

    displayWidth = width;
    displayHeight = height;

    SDL_RenderSetLogicalSize(sdlRenderer, displayWidth, displayHeight);

    if (sdlTexture)
    {
        SDL_DestroyTexture(sdlTexture);
        sdlTexture = NULL;
    }

    sdlTexture = SDL_CreateTexture(sdlRenderer,
                                   SDL_PIXELFORMAT_ABGR1555,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   displayWidth, displayHeight);
    if (sdlTexture == NULL)
    {
        fprintf(stderr, "Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        return FALSE;
    }

    printf("Set resolution to %dx%d (scale %d)\n", width, height, videoScale);

    return TRUE;
}

static bool8 InitVideo(void)
{
    s32 scrW, scrH;

    int sdlRendererFlags = 0;

    videoScale = 1;

    scrW = BASE_DISPLAY_WIDTH;
    scrH = BASE_DISPLAY_HEIGHT;

    windowWidth = scrW * videoScale;
    windowHeight = scrH * videoScale;

    sdlWindow = SDL_CreateWindow("pokeemerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (sdlWindow == NULL)
    {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return FALSE;
    }

#if SDL_VERSION_ATLEAST(2, 0, 18)
    sdlRendererFlags |= SDL_RENDERER_PRESENTVSYNC;
#endif

    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, sdlRendererFlags);
    if (sdlRenderer == NULL)
    {
        fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return FALSE;
    }

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetWindowMinimumSize(sdlWindow, BASE_DISPLAY_WIDTH, BASE_DISPLAY_HEIGHT);

    if (SetResolution(scrW, scrH) == FALSE)
    {
        return FALSE;
    }

    for (unsigned i = 0; i < NUM_BACKGROUNDS + 1; i++)
        layerEnabled[i] = TRUE;

    return TRUE;
}

static void InitAudio(void)
{
    SDL_AudioSpec want;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = 42048;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    cgb_audio_init(want.freq);

    if (SDL_OpenAudio(&want, 0) < 0)
        SDL_Log("Failed to open audio: %s", SDL_GetError());
    else
    {
        if (want.format != AUDIO_F32) /* we let this one thing change. */
            SDL_Log("We didn't get Float32 audio format.");
        SDL_PauseAudio(0);
    }
}

static void InitTime(void)
{
    memset(&internalClock, 0, sizeof(internalClock));
    internalClock.status = SIIRTCINFO_24HOUR;
    UpdateInternalClock();
}

int main(int argc, char **argv)
{
    // Open an output console on Windows
#ifdef _WIN32
    AllocConsole() ;
    AttachConsole( GetCurrentProcessId() ) ;
    freopen( "CON", "w", stdout ) ;
#endif

    ReadSaveFile("pokeemerald.sav");

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    if (InitVideo() == FALSE)
    {
        return 1;
    }

    simTime = curGameTime = lastGameTime = SDL_GetPerformanceCounter();

    InitAudio();

    InitTime();

    GameInit();

#ifdef USE_THREAD
    isFrameAvailable.value = 0;
    vBlankSemaphore = SDL_CreateSemaphore(0);

    mainLoopThread = SDL_CreateThread(DoMain, "AgbMain", NULL);
#endif

    double accumulator = 0.0;

    while (isRunning)
    {
        ProcessEvents();

        if (videoScaleChanged)
        {
            SDL_SetWindowSize(sdlWindow, windowWidth, windowHeight);

            if (recenterWindow)
            {
                SDL_SetWindowPosition(sdlWindow,
                    SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetWindowDisplayIndex(sdlWindow)),
                    SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetWindowDisplayIndex(sdlWindow))
                );

                recenterWindow = false;
            }

            videoScaleChanged = false;
        }

        if (!paused)
        {
            double dt = fixedTimestep / timeScale; // TODO: Fix speedup

            curGameTime = SDL_GetPerformanceCounter();
            double deltaTime = (double)((curGameTime - lastGameTime) / (double)SDL_GetPerformanceFrequency());
            if (deltaTime > (dt * 5))
                deltaTime = dt;
            lastGameTime = curGameTime;

            accumulator += deltaTime;

            while (accumulator >= dt)
            {
#ifdef USE_THREAD
                if (SDL_AtomicGet(&isFrameAvailable))
                {
                    SDL_AtomicSet(&isFrameAvailable, 0);

                    RunFrame();

                    SDL_SemPost(vBlankSemaphore);

                    accumulator -= dt;
                }
#else
                RunFrame();

                accumulator -= dt;
#endif
            }

            // Draws each scanline
            RenderFrame(sdlTexture);

            // Calls m4aSoundMain() and m4aSoundVSync()
            AudioUpdate();
        }

        // Display the frame
        SDL_RenderClear(sdlRenderer);
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
        SDL_RenderPresent(sdlRenderer);
    }

    //StoreSaveFile();
    CloseSaveFile();

    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
    return 0;
}

static void ReadSaveFile(char *path)
{
    // Check whether the saveFile exists, and create it if not
    sSaveFile = fopen(path, "r+b");
    if (sSaveFile == NULL)
    {
        sSaveFile = fopen(path, "w+b");
    }

    fseek(sSaveFile, 0, SEEK_END);
    int fileSize = ftell(sSaveFile);
    fseek(sSaveFile, 0, SEEK_SET);

    // Only read as many bytes as fit inside the buffer
    // or as many bytes as are in the file
    int bytesToRead = (fileSize < sizeof(FLASH_BASE)) ? fileSize : sizeof(FLASH_BASE);

    int bytesRead = fread(FLASH_BASE, 1, bytesToRead, sSaveFile);

    // Fill the buffer if the savefile was just created or smaller than the buffer itself
    for (int i = bytesRead; i < sizeof(FLASH_BASE); i++)
    {
        FLASH_BASE[i] = 0xFF;
    }
}

static void StoreSaveFile()
{
    if (sSaveFile != NULL)
    {
        fseek(sSaveFile, 0, SEEK_SET);
        fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), sSaveFile);
    }
}

void Platform_StoreSaveFile(void)
{
    StoreSaveFile();
}

static void CloseSaveFile()
{
    if (sSaveFile != NULL)
    {
        fclose(sSaveFile);
    }
}

static void SetVideoScale(int scale)
{
    if (scale < 1 || scale > 4)
        return;

    videoScale = scale;
    windowWidth = displayWidth * videoScale;
    windowHeight = displayHeight * videoScale;
    videoScaleChanged = true;
    recenterWindow = true;

    printf("Set video scale to %d\n", videoScale);
}

// Key mappings
#define KEY_A_BUTTON      SDLK_z
#define KEY_B_BUTTON      SDLK_x
#define KEY_START_BUTTON  SDLK_RETURN
#define KEY_SELECT_BUTTON SDLK_BACKSLASH
#define KEY_L_BUTTON      SDLK_a
#define KEY_R_BUTTON      SDLK_s
#define KEY_DPAD_UP       SDLK_UP
#define KEY_DPAD_DOWN     SDLK_DOWN
#define KEY_DPAD_LEFT     SDLK_LEFT
#define KEY_DPAD_RIGHT    SDLK_RIGHT

#define HANDLE_KEYUP(key) \
case KEY_##key:  keys &= ~key; break;

#define HANDLE_KEYDOWN(key) \
case KEY_##key:  keys |= key; break;

static u16 keys;

void ProcessEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            isRunning = false;
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYUP(A_BUTTON)
            HANDLE_KEYUP(B_BUTTON)
            HANDLE_KEYUP(START_BUTTON)
            HANDLE_KEYUP(SELECT_BUTTON)
            HANDLE_KEYUP(L_BUTTON)
            HANDLE_KEYUP(R_BUTTON)
            HANDLE_KEYUP(DPAD_UP)
            HANDLE_KEYUP(DPAD_DOWN)
            HANDLE_KEYUP(DPAD_LEFT)
            HANDLE_KEYUP(DPAD_RIGHT)
            case SDLK_SPACE:
                if (speedUp)
                {
                    speedUp = false;
                    timeScale = 1.0;
                }
                break;
            }
            break;
        case SDL_KEYDOWN:
            if (event.key.state != SDL_PRESSED)
                break;
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYDOWN(A_BUTTON)
            HANDLE_KEYDOWN(B_BUTTON)
            HANDLE_KEYDOWN(START_BUTTON)
            HANDLE_KEYDOWN(SELECT_BUTTON)
            HANDLE_KEYDOWN(L_BUTTON)
            HANDLE_KEYDOWN(R_BUTTON)
            HANDLE_KEYDOWN(DPAD_UP)
            HANDLE_KEYDOWN(DPAD_DOWN)
            HANDLE_KEYDOWN(DPAD_LEFT)
            HANDLE_KEYDOWN(DPAD_RIGHT)
            case SDLK_r:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    DoSoftReset();
                }
                break;
            case SDLK_p:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    paused = !paused;

                    if (paused)
                    {
                        SDL_PauseAudio(1);
                    }
                    else
                    {
                        SDL_ClearQueuedAudio(1);
                        SDL_PauseAudio(0);
                    }
                }
                break;
            case SDLK_SPACE:
                if (!speedUp)
                {
                    speedUp = true;
                    timeScale = 5.0;
                }
                break;
            case SDLK_v:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    runVBlank = !runVBlank;
                    if (runVBlank) {
                        printf("Enabled VBlank\n");
                    }
                    else {
                        printf("Disabled VBlank\n");
                    }
                }
                break;
            case SDLK_h:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    runHBlank = !runHBlank;
                    if (runHBlank) {
                        printf("Enabled HBlank\n");
                    }
                    else {
                        printf("Disabled HBlank\n");
                    }
                }
                break;
            case SDLK_KP_MINUS:
                SetVideoScale(videoScale - 1);
                break;
            case SDLK_KP_PLUS:
                SetVideoScale(videoScale + 1);
                break;
            default: {
                int key = event.key.keysym.sym;
                if (key >= SDLK_1 && key <= SDLK_5) {
                    key -= SDLK_1;
                    layerEnabled[key] = !layerEnabled[key];
                    if (layerEnabled[key]) {
                        if (key == 5)
                            printf("Enabled sprite layer\n");
                        else
                            printf("Enabled BG layer %d\n", key + 1);
                    }
                    else {
                        if (key == 5)
                            printf("Disabled sprite layer\n");
                        else
                            printf("Disabled BG layer %d\n", key + 1);
                    }
                }
                break;
                }
            }
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                unsigned int w = event.window.data1;
                unsigned int h = event.window.data2;

                int scaleW, scaleH;

                windowWidth = w;
                windowHeight = h;

                if (w < BASE_DISPLAY_WIDTH)
                    w = BASE_DISPLAY_WIDTH;
                if (h < BASE_DISPLAY_HEIGHT)
                    h = BASE_DISPLAY_HEIGHT;

                scaleW = w / BASE_DISPLAY_WIDTH;
                scaleH = h / BASE_DISPLAY_HEIGHT;

                videoScale = scaleW < scaleH ? scaleW : scaleH;

                w /= videoScale;
                h /= videoScale;

#ifdef ALLOW_ANY_RESOLUTION
                if (SetResolution(w, h) == FALSE)
                    abort();
#endif

                videoScaleChanged = true;
            }
            break;
        }
    }
}

#ifdef _WIN32
#define STICK_THRESHOLD 0.5f
u16 GetXInputKeys()
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    DWORD dwResult = XInputGetState(0, &state);
    u16 xinputKeys = 0;

    if (dwResult == ERROR_SUCCESS)
    {
        /* A */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) >> 12;
        /* B */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) >> 13;
        /* Start */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) >> 1;
        /* Select */ xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) >> 3;
        /* L */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) << 1;
        /* R */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) >> 1;
        /* Up */     xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) << 6;
        /* Down */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) << 6;
        /* Left */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) << 3;
        /* Right */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) << 1;


        /* Control Stick */
        float xAxis = (float)state.Gamepad.sThumbLX / (float)SHRT_MAX;
        float yAxis = (float)state.Gamepad.sThumbLY / (float)SHRT_MAX;

        if (xAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_LEFT;
        if (xAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_RIGHT;
        if (yAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_DOWN;
        if (yAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_UP;


        /* Speedup */
        // Note: 'speedup' variable is only (un)set on keyboard input
        double oldTimeScale = timeScale;
        timeScale = (state.Gamepad.bRightTrigger > 0x80 || speedUp) ? 5.0 : 1.0;
    }

    return xinputKeys;
}
#endif // _WIN32

u16 Platform_GetKeyInput(void)
{
#ifdef _WIN32
    u16 gamepadKeys = GetXInputKeys();
    return (gamepadKeys != 0) ? gamepadKeys : keys;
#endif

    return keys;
}

// BIOS function implementations are based on the VBA-M source code.

static uint32_t CPUReadMemory(const void *src)
{
    return *(uint32_t *)src;
}

static void CPUWriteMemory(void *dest, uint32_t val)
{
    *(uint32_t *)dest = val;
}

static uint16_t CPUReadHalfWord(const void *src)
{
    return *(uint16_t *)src;
}

static void CPUWriteHalfWord(void *dest, uint16_t val)
{
    *(uint16_t *)dest = val;
}

static uint8_t CPUReadByte(const void *src)
{
    return *(uint8_t *)src;
}

static void CPUWriteByte(void *dest, uint8_t val)
{
    *(uint8_t *)dest = val;
}

void LZ77UnCompVram(const u32 *src_, void *dest_)
{
    const u8 *src = src_;
    u8 *dest = dest_;
    int destSize = (src[3] << 16) | (src[2] << 8) | src[1];
    int srcPos = 4;
    int destPos = 0;

    for (;;) {
        unsigned char flags = src[srcPos++];

        for (int i = 0; i < 8; i++) {
            if (flags & 0x80) {
                int blockSize = (src[srcPos] >> 4) + 3;
                int blockDistance = (((src[srcPos] & 0xF) << 8) | src[srcPos + 1]) + 1;

                srcPos += 2;

                int blockPos = destPos - blockDistance;

                // Some Ruby/Sapphire tilesets overflow.
                if (destPos + blockSize > destSize) {
                    blockSize = destSize - destPos;
                    //fprintf(stderr, "Destination buffer overflow.\n");
                    puts("Destination buffer overflow.\n");
                }

                if (blockPos < 0)
                    goto fail;

                for (int j = 0; j < blockSize; j++)
                    dest[destPos++] = dest[blockPos + j];
            } else {
                if (destPos >= destSize)
                    goto fail;

                dest[destPos++] = src[srcPos++];
            }

            if (destPos == destSize) {
                return;
            }

            flags <<= 1;
        }
    }

fail:
    puts("Fatal error while decompressing LZ file.\n");
}

void LZ77UnCompWram(const u32 *src, void *dst)
{
    const uint8_t *source = src;
    uint8_t *dest = dst;

    uint32_t header = CPUReadMemory(source);
    source += 4;

    int len = header >> 8;

    while (len > 0) {
        uint8_t d = CPUReadByte(source++);

        if (d) {
            for (int i = 0; i < 8; i++) {
                if (d & 0x80) {
                    uint16_t data = CPUReadByte(source++) << 8;
                    data |= CPUReadByte(source++);
                    int length = (data >> 12) + 3;
                    int offset = (data & 0x0FFF);
                    uint8_t *windowOffset = dest - offset - 1;
                    for (int i2 = 0; i2 < length; i2++) {
                        CPUWriteByte(dest++, CPUReadByte(windowOffset++));
                        len--;
                        if (len == 0)
                            return;
                    }
                } else {
                    CPUWriteByte(dest++, CPUReadByte(source++));
                    len--;
                    if (len == 0)
                        return;
                }
                d <<= 1;
            }
        } else {
            for (int i = 0; i < 8; i++) {
                CPUWriteByte(dest++, CPUReadByte(source++));
                len--;
                if (len == 0)
                    return;
            }
        }
    }
}

void RLUnCompWram(const void *src, void *dest)
{
    int remaining = CPUReadMemory(src) >> 8;
    int padding = (4 - remaining) & 0x3;
    int blockHeader;
    int block;
    u32 *dest_u32 = (u32*)dest;
    src += 4;
    while (remaining > 0)
    {
        blockHeader = CPUReadByte(src);
        src++;
        if (blockHeader & 0x80) // Compressed?
        {
            blockHeader &= 0x7F;
            blockHeader += 3;
            block = CPUReadByte(src);
            src++;
            while (blockHeader-- && remaining)
            {
                remaining--;
                CPUWriteByte(dest_u32, block);
                dest_u32++;
            }
        }
        else // Uncompressed
        {
            blockHeader++;
            while (blockHeader-- && remaining)
            {
                remaining--;
                u8 byte = CPUReadByte(src);
                src++;
                CPUWriteByte(dest_u32, byte);
                dest_u32++;
            }
        }
    }
    while (padding--)
    {
        CPUWriteByte(dest_u32, 0);
        dest_u32++;
    }
}

void RLUnCompVram(const void *src, void *dest)
{
    int remaining = CPUReadMemory(src) >> 8;
    int padding = (4 - remaining) & 0x3;
    int blockHeader;
    int block;
    int halfWord = 0;
    u32 *dest_u32 = (u32*)dest;
    src += 4;
    while (remaining > 0)
    {
        blockHeader = CPUReadByte(src);
        src++;
        if (blockHeader & 0x80) // Compressed?
        {
            blockHeader &= 0x7F;
            blockHeader += 3;
            block = CPUReadByte(src);
            src++;
            while (blockHeader-- && remaining)
            {
                remaining--;
                if ((u32)dest_u32 & 1)
                {
                    halfWord |= block << 8;
                    CPUWriteHalfWord((u32)dest_u32 ^ 1, halfWord);
                }
                else
                    halfWord = block;
                dest_u32++;
            }
        }
        else // Uncompressed
        {
            blockHeader++;
            while (blockHeader-- && remaining)
            {
                remaining--;
                u8 byte = CPUReadByte(src);
                src++;
                if ((u32)dest_u32 & 1)
                {
                    halfWord |= byte << 8;
                    CPUWriteHalfWord((u32)dest_u32 ^ 1, halfWord);
                }
                else
                    halfWord = byte;
                dest_u32++;
            }
        }
    }
    if ((u32)dest_u32 & 1)
    {
        padding--;
        dest_u32++;
    }
    for (; padding > 0; padding -= 2, dest_u32 += 2)
        CPUWriteHalfWord(dest_u32, 0);
}

const s16 sineTable[256] = {
  (s16)0x0000, (s16)0x0192, (s16)0x0323, (s16)0x04B5, (s16)0x0645, (s16)0x07D5, (s16)0x0964, (s16)0x0AF1,
  (s16)0x0C7C, (s16)0x0E05, (s16)0x0F8C, (s16)0x1111, (s16)0x1294, (s16)0x1413, (s16)0x158F, (s16)0x1708,
  (s16)0x187D, (s16)0x19EF, (s16)0x1B5D, (s16)0x1CC6, (s16)0x1E2B, (s16)0x1F8B, (s16)0x20E7, (s16)0x223D,
  (s16)0x238E, (s16)0x24DA, (s16)0x261F, (s16)0x275F, (s16)0x2899, (s16)0x29CD, (s16)0x2AFA, (s16)0x2C21,
  (s16)0x2D41, (s16)0x2E5A, (s16)0x2F6B, (s16)0x3076, (s16)0x3179, (s16)0x3274, (s16)0x3367, (s16)0x3453,
  (s16)0x3536, (s16)0x3612, (s16)0x36E5, (s16)0x37AF, (s16)0x3871, (s16)0x392A, (s16)0x39DA, (s16)0x3A82,
  (s16)0x3B20, (s16)0x3BB6, (s16)0x3C42, (s16)0x3CC5, (s16)0x3D3E, (s16)0x3DAE, (s16)0x3E14, (s16)0x3E71,
  (s16)0x3EC5, (s16)0x3F0E, (s16)0x3F4E, (s16)0x3F84, (s16)0x3FB1, (s16)0x3FD3, (s16)0x3FEC, (s16)0x3FFB,
  (s16)0x4000, (s16)0x3FFB, (s16)0x3FEC, (s16)0x3FD3, (s16)0x3FB1, (s16)0x3F84, (s16)0x3F4E, (s16)0x3F0E,
  (s16)0x3EC5, (s16)0x3E71, (s16)0x3E14, (s16)0x3DAE, (s16)0x3D3E, (s16)0x3CC5, (s16)0x3C42, (s16)0x3BB6,
  (s16)0x3B20, (s16)0x3A82, (s16)0x39DA, (s16)0x392A, (s16)0x3871, (s16)0x37AF, (s16)0x36E5, (s16)0x3612,
  (s16)0x3536, (s16)0x3453, (s16)0x3367, (s16)0x3274, (s16)0x3179, (s16)0x3076, (s16)0x2F6B, (s16)0x2E5A,
  (s16)0x2D41, (s16)0x2C21, (s16)0x2AFA, (s16)0x29CD, (s16)0x2899, (s16)0x275F, (s16)0x261F, (s16)0x24DA,
  (s16)0x238E, (s16)0x223D, (s16)0x20E7, (s16)0x1F8B, (s16)0x1E2B, (s16)0x1CC6, (s16)0x1B5D, (s16)0x19EF,
  (s16)0x187D, (s16)0x1708, (s16)0x158F, (s16)0x1413, (s16)0x1294, (s16)0x1111, (s16)0x0F8C, (s16)0x0E05,
  (s16)0x0C7C, (s16)0x0AF1, (s16)0x0964, (s16)0x07D5, (s16)0x0645, (s16)0x04B5, (s16)0x0323, (s16)0x0192,
  (s16)0x0000, (s16)0xFE6E, (s16)0xFCDD, (s16)0xFB4B, (s16)0xF9BB, (s16)0xF82B, (s16)0xF69C, (s16)0xF50F,
  (s16)0xF384, (s16)0xF1FB, (s16)0xF074, (s16)0xEEEF, (s16)0xED6C, (s16)0xEBED, (s16)0xEA71, (s16)0xE8F8,
  (s16)0xE783, (s16)0xE611, (s16)0xE4A3, (s16)0xE33A, (s16)0xE1D5, (s16)0xE075, (s16)0xDF19, (s16)0xDDC3,
  (s16)0xDC72, (s16)0xDB26, (s16)0xD9E1, (s16)0xD8A1, (s16)0xD767, (s16)0xD633, (s16)0xD506, (s16)0xD3DF,
  (s16)0xD2BF, (s16)0xD1A6, (s16)0xD095, (s16)0xCF8A, (s16)0xCE87, (s16)0xCD8C, (s16)0xCC99, (s16)0xCBAD,
  (s16)0xCACA, (s16)0xC9EE, (s16)0xC91B, (s16)0xC851, (s16)0xC78F, (s16)0xC6D6, (s16)0xC626, (s16)0xC57E,
  (s16)0xC4E0, (s16)0xC44A, (s16)0xC3BE, (s16)0xC33B, (s16)0xC2C2, (s16)0xC252, (s16)0xC1EC, (s16)0xC18F,
  (s16)0xC13B, (s16)0xC0F2, (s16)0xC0B2, (s16)0xC07C, (s16)0xC04F, (s16)0xC02D, (s16)0xC014, (s16)0xC005,
  (s16)0xC000, (s16)0xC005, (s16)0xC014, (s16)0xC02D, (s16)0xC04F, (s16)0xC07C, (s16)0xC0B2, (s16)0xC0F2,
  (s16)0xC13B, (s16)0xC18F, (s16)0xC1EC, (s16)0xC252, (s16)0xC2C2, (s16)0xC33B, (s16)0xC3BE, (s16)0xC44A,
  (s16)0xC4E0, (s16)0xC57E, (s16)0xC626, (s16)0xC6D6, (s16)0xC78F, (s16)0xC851, (s16)0xC91B, (s16)0xC9EE,
  (s16)0xCACA, (s16)0xCBAD, (s16)0xCC99, (s16)0xCD8C, (s16)0xCE87, (s16)0xCF8A, (s16)0xD095, (s16)0xD1A6,
  (s16)0xD2BF, (s16)0xD3DF, (s16)0xD506, (s16)0xD633, (s16)0xD767, (s16)0xD8A1, (s16)0xD9E1, (s16)0xDB26,
  (s16)0xDC72, (s16)0xDDC3, (s16)0xDF19, (s16)0xE075, (s16)0xE1D5, (s16)0xE33A, (s16)0xE4A3, (s16)0xE611,
  (s16)0xE783, (s16)0xE8F8, (s16)0xEA71, (s16)0xEBED, (s16)0xED6C, (s16)0xEEEF, (s16)0xF074, (s16)0xF1FB,
  (s16)0xF384, (s16)0xF50F, (s16)0xF69C, (s16)0xF82B, (s16)0xF9BB, (s16)0xFB4B, (s16)0xFCDD, (s16)0xFE6E
};

void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, s32 count)
{
    for(s32 i=0; i<count; i++)
    {
        s32 cx = src[i].texX;
        s32 cy = src[i].texY;
        s16 dispx = src[i].scrX;
        s16 dispy = src[i].scrY;
        s16 rx = src[i].sx;
        s16 ry = src[i].sy;
        u16 theta = src[i].alpha>>8;
        s32 a = sineTable[(theta+0x40)&255];
        s32 b = sineTable[theta];

        s16 dx =  (rx * a)>>14;
        s16 dmx = (rx * b)>>14;
        s16 dy =  (ry * b)>>14;
        s16 dmy = (ry * a)>>14;

        dest[i].pa = dx;
        dest[i].pb = -dmx;
        dest[i].pc = dy;
        dest[i].pd = dmy;

        s32 startx = cx - dx * dispx + dmx * dispy;
        s32 starty = cy - dy * dispx - dmy * dispy;

        dest[i].dx = startx;
        dest[i].dy = starty;
    }
}

void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, s32 count, s32 offset)
{
    for(s32 i=0; i<count; i++)
    {
        s16 rx = src[i].xScale;
        s16 ry = src[i].yScale;
        u16 theta = src[i].rotation>>8;

        s32 a = (s32)sineTable[(theta + 64) & 255];
        s32 b = (s32)sineTable[theta];

        s16 dx =  ((s32)rx * a)>>14;
        s16 dmx = ((s32)rx * b)>>14;
        s16 dy =  ((s32)ry * b)>>14;
        s16 dmy = ((s32)ry * a)>>14;

        CPUWriteHalfWord(dest, dx);
        dest += offset;
        CPUWriteHalfWord(dest, -dmx);
        dest += offset;
        CPUWriteHalfWord(dest, dy);
        dest += offset;
        CPUWriteHalfWord(dest, dmy);
        dest += offset;
    }
}

void SoftReset(u32 resetFlags)
{
    puts("Soft Reset called. Exiting.");
    exit(0);
}

#define mosaicBGEffectX (gpu.mosaic & 0xF)
#define mosaicBGEffectY ((gpu.mosaic >> 4) & 0xF)
#define mosaicSpriteEffectX ((gpu.mosaic >> 8) & 0xF)
#define mosaicSpriteEffectY ((gpu.mosaic >> 12) & 0xF)
#define applyBGHorizontalMosaicEffect(x) (x - (x % (mosaicBGEffectX+1)))
#define applyBGVerticalMosaicEffect(y) (y - (y % (mosaicBGEffectY+1)))
#define applySpriteHorizontalMosaicEffect(x) (x - (x % (mosaicSpriteEffectX+1)))
#define applySpriteVerticalMosaicEffect(y) (y - (y % (mosaicSpriteEffectY+1)))

static void GetBGScanlinePos(int bgNum, int *lineStart, int *lineEnd)
{
    struct GpuBgState *bg = &gpu.bg[bgNum];

    *lineStart = 0;
    *lineEnd = displayWidth;

    if (bg->gbaMode)
    {
        int offsetX = (displayWidth - BASE_DISPLAY_WIDTH) / 2;
        *lineStart += offsetX;
        *lineEnd = BASE_DISPLAY_WIDTH + offsetX;
    }
}

static void RenderBGScanline(int bgNum, uint16_t hoffs, uint16_t voffs, int lineNum, uint16_t *line)
{
    struct GpuBgState *bg = &gpu.bg[bgNum];
    unsigned int bitsPerPixel = bg->palettes ? 8 : 4;
    unsigned int mapWidthInPixels = bg->screenWidth;
    unsigned int mapHeightInPixels = bg->screenHeight;
    unsigned int mapWidth;

    int lineStart, lineEnd;
    GetBGScanlinePos(bgNum, &lineStart, &lineEnd);

    uint8_t *bgtiles = (uint8_t *)BG_CHAR_ADDR(bg->charBaseBlock);
    uint16_t *pal = (uint16_t *)gpu.palette;

    if (bg->gbaMode)
    {
        int offsetY = (displayHeight - BASE_DISPLAY_HEIGHT) / 2;

        lineNum -= offsetY;

        if (lineNum < 0 || lineNum >= BASE_DISPLAY_HEIGHT)
            return;

        hoffs &= 0x1FF;
        voffs &= 0x1FF;

        if (mapWidthInPixels > 256)
            mapWidthInPixels = 512;
        else
            mapWidthInPixels = 256;

        if (mapHeightInPixels > 256)
            mapHeightInPixels = 512;
        else
            mapHeightInPixels = 256;

        mapWidth = 0x20;
    }
    else
    {
        mapWidth = mapWidthInPixels / 8;
    }

    if (bg->mosaic)
        lineNum = applyBGVerticalMosaicEffect(lineNum);

    for (unsigned int x = lineStart; x < lineEnd; x++)
    {
        uint16_t *bgmap = (uint16_t *)BG_SCREEN_ADDR(bg->screenBaseBlock);

        // adjust for scroll
        unsigned int xx;
        if (bg->mosaic)
            xx = applyBGHorizontalMosaicEffect(x) + hoffs;
        else
            xx = x + hoffs;

        xx -= lineStart;

        unsigned int yy = lineNum + voffs;

        if (bg->gbaMode)
        {
            xx &= 0x1FF;
            yy &= 0x1FF;

            //if x or y go above 255 pixels it goes to the next screen base which are (BG_SCREEN_SIZE / 2) WORDs long
            if (xx > 255 && mapWidthInPixels == 512) {
                bgmap += GBA_BG_SCREEN_SIZE / 2;
            }

            if (yy > 255 && mapHeightInPixels == 512) {
                //the width check is for 512x512 mode support, it jumps by two screen bases instead
                bgmap += (mapWidthInPixels == 512) ? GBA_BG_SCREEN_SIZE : GBA_BG_SCREEN_SIZE / 2;
            }

            //maximum width for bgtile block is 256
            xx &= 0xFF;
            yy &= 0xFF;
        }
        else
        {
            xx %= mapWidthInPixels;
            yy %= mapHeightInPixels;
        }

        unsigned int mapX = xx / 8;
        unsigned int mapY = yy / 8;
        uint16_t entry = bgmap[(mapY * mapWidth) + mapX];

        unsigned int tileNum = entry & 0x3FF;
        unsigned int paletteNum = (entry >> 12) & 0xF;

        unsigned int tileX = xx % 8;
        unsigned int tileY = yy % 8;

        // Flip if necessary
        if (entry & (1 << 10))
            tileX = 7 - tileX;
        if (entry & (1 << 11))
            tileY = 7 - tileY;

        uint16_t tileLoc = tileNum * (bitsPerPixel * 8);
        uint16_t tileLocY = tileY * bitsPerPixel;
        uint16_t tileLocX = tileX;
        if (bitsPerPixel == 4)
            tileLocX /= 2;

        uint8_t pixel = bgtiles[tileLoc + tileLocY + tileLocX];

        if (bitsPerPixel == 4) {
            if (tileX & 1)
                pixel >>= 4;
            else
                pixel &= 0xF;

            if (pixel != 0)
                line[x] = pal[16 * paletteNum + pixel] | 0x8000;
        }
        else {
            line[x] = pal[pixel] | 0x8000;
        }
    }
}

static inline uint32_t getAffineBgX(int bgNumber)
{
    if (bgNumber == 2)
    {
        return gpu.affineBg2.x;
    }
    else if (bgNumber == 3)
    {
        return gpu.affineBg3.x;
    }

    return 0;
}

static inline uint32_t getAffineBgY(int bgNumber)
{
    if (bgNumber == 2)
    {
        return gpu.affineBg2.y;
    }
    else if (bgNumber == 3)
    {
        return gpu.affineBg3.y;
    }

    return 0;
}

static inline uint16_t getBgPA(int bgNumber)
{
    if (bgNumber == 2)
    {
        return gpu.affineBg2.pa;
    }
    else if (bgNumber == 3)
    {
        return gpu.affineBg3.pa;
    }

    return 0;
}

static inline uint16_t getBgPB(int bgNumber)
{
    if (bgNumber == 2)
    {
        return gpu.affineBg2.pb;
    }
    else if (bgNumber == 3)
    {
        return gpu.affineBg3.pb;
    }

    return 0;
}

static inline uint16_t getBgPC(int bgNumber)
{
    if (bgNumber == 2)
    {
        return gpu.affineBg2.pc;
    }
    else if (bgNumber == 3)
    {
        return gpu.affineBg3.pc;
    }

    return 0;
}

static inline uint16_t getBgPD(int bgNumber)
{
    if (bgNumber == 2)
    {
        return gpu.affineBg2.pd;
    }
    else if (bgNumber == 3)
    {
        return gpu.affineBg3.pd;
    }

    return 0;
}

static void RenderRotScaleBGScanline(int bgNum, uint16_t x, uint16_t y, int lineNum, uint16_t *line)
{
    struct GpuBgState *bg = &gpu.bg[bgNum];

    uint8_t *bgtiles = (uint8_t *)BG_CHAR_ADDR(bg->charBaseBlock);
    uint8_t *bgmap = (uint8_t *)BG_SCREEN_ADDR(bg->screenBaseBlock);
    uint16_t *pal = (uint16_t *)gpu.palette;

    if (bg->mosaic)
        lineNum = applyBGVerticalMosaicEffect(lineNum);

    s16 pa = getBgPA(bgNum);
    s16 pb = getBgPB(bgNum);
    s16 pc = getBgPC(bgNum);
    s16 pd = getBgPD(bgNum);

    int sizeX = 1;
    int sizeY = 1;
    int yshift = 0;

    switch (bg->screenWidth)
    {
    case 128:
        sizeX = sizeY = 128;
        yshift = 4;
        break;
    case 256:
        sizeX = sizeY = 256;
        yshift = 5;
        break;
    case 512:
        sizeX = sizeY = 512;
        yshift = 6;
        break;
    case 1024:
        sizeX = sizeY = 1024;
        yshift = 7;
        break;
    default:
        return;
    }

    int maskX = sizeX - 1;
    int maskY = sizeY - 1;

    s32 currentX = getAffineBgX(bgNum);
    s32 currentY = getAffineBgY(bgNum);
    //sign extend 28 bit number
    currentX = ((currentX & (1 << 27)) ? currentX | 0xF0000000 : currentX);
    currentY = ((currentY & (1 << 27)) ? currentY | 0xF0000000 : currentY);

    currentX += lineNum * pb;
    currentY += lineNum * pd;

    int realX = currentX;
    int realY = currentY;

    if (bg->areaOverflowMode)
    {
        for (int x = 0; x < displayWidth; x++)
        {
            int xxx = (realX >> 8) & maskX;
            int yyy = (realY >> 8) & maskY;

            int tile = bgmap[(xxx >> 3) + ((yyy >> 3) << yshift)];

            int tileX = xxx & 7;
            int tileY = yyy & 7;

            uint8_t pixel = bgtiles[(tile << 6) + (tileY << 3) + tileX];

            if (pixel != 0) {
                line[x] = pal[pixel] | 0x8000;
            }

            realX += pa;
            realY += pc;
        }
    }
    else
    {
        for (int x = 0; x < displayWidth; x++)
        {
            int xxx = (realX >> 8);
            int yyy = (realY >> 8);

            if (xxx < 0 || yyy < 0 || xxx >= sizeX || yyy >= sizeY)
            {
                //line[x] = 0x80000000;
            }
            else
            {
                int tile = bgmap[(xxx >> 3) + ((yyy >> 3) << yshift)];

                int tileX = xxx & 7;
                int tileY = yyy & 7;

                uint8_t pixel = bgtiles[(tile << 6) + (tileY << 3) + tileX];

                if (pixel != 0) {
                    line[x] = pal[pixel] | 0x8000;
                }
            }
            realX += pa;
            realY += pc;
        }
    }
    //the only way i could figure out how to get accurate mosaic on affine bgs 
    //luckily i dont think pokemon emerald uses mosaic on affine bgs
    if (bg->mosaic && mosaicBGEffectX > 0)
    {
        for (int x = 0; x < displayWidth; x++)
        {
            uint16_t color = line[applyBGHorizontalMosaicEffect(x)];
            line[x] = color;
        }
    }
}

const u8 spriteSizes[][2] =
{
    {8, 16},
    {8, 32},
    {16, 32},
    {32, 64},
};

#define getAlphaBit(x) ((x >> 15) & 1)
#define getRedChannel(x) ((x >>  0) & 0x1F)
#define getGreenChannel(x) ((x >>  5) & 0x1F)
#define getBlueChannel(x) ((x >>  10) & 0x1F)
#define isbgEnabled(x) ((gpu.displayControl >> 8) & 0xF) & (1 << x)

static uint16_t alphaBlendColor(uint16_t targetA, uint16_t targetB)
{
    unsigned int eva = gpu.blendAlpha & 0x1F;
    unsigned int evb = (gpu.blendAlpha >> 8) & 0x1F;
    // shift right by 4 = division by 16
    unsigned int r = ((getRedChannel(targetA) * eva) + (getRedChannel(targetB) * evb)) >> 4;
    unsigned int g = ((getGreenChannel(targetA) * eva) + (getGreenChannel(targetB) * evb)) >> 4;
    unsigned int b = ((getBlueChannel(targetA) * eva) + (getBlueChannel(targetB) * evb)) >> 4;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;

    return r | (g << 5) | (b << 10) | (1 << 15);
}

static uint16_t alphaBrightnessIncrease(uint16_t targetA)
{
    unsigned int evy = (gpu.blendCoeff & 0x1F);
    unsigned int r = getRedChannel(targetA) + (31 - getRedChannel(targetA)) * evy / 16;
    unsigned int g = getGreenChannel(targetA) + (31 - getGreenChannel(targetA)) * evy / 16;
    unsigned int b = getBlueChannel(targetA) + (31 - getBlueChannel(targetA)) * evy / 16;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;
    
    return r | (g << 5) | (b << 10) | (1 << 15);
}

static uint16_t alphaBrightnessDecrease(uint16_t targetA)
{
    unsigned int evy = (gpu.blendCoeff & 0x1F);
    unsigned int r = getRedChannel(targetA) - getRedChannel(targetA) * evy / 16;
    unsigned int g = getGreenChannel(targetA) - getGreenChannel(targetA) * evy / 16;
    unsigned int b = getBlueChannel(targetA) - getBlueChannel(targetA) * evy / 16;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;
    
    return r | (g << 5) | (b << 10) | (1 << 15);
}

//outputs the blended pixel in colorOutput, the prxxx are the bg priority and subpriority, pixelpos is pixel offset in scanline
static bool alphaBlendSelectTargetB(struct scanlineData* scanline, uint16_t* colorOutput, char prnum, char prsub, int pixelpos, bool spriteBlendEnabled)
{   
    //iterate through every possible bg to blend with, starting from specified priorities from arguments
    for (unsigned int blndprnum = prnum; blndprnum <= 3; blndprnum++)
    {
        //check if sprite is available to blend with, if sprite blending is enabled
        if (spriteBlendEnabled == true && getAlphaBit(scanline->spriteLayers[blndprnum][pixelpos]) == 1)
        {
            *colorOutput = scanline->spriteLayers[blndprnum][pixelpos];
            return true;
        }
            
        for (unsigned int blndprsub = prsub; blndprsub < scanline->prioritySortedBgsCount[blndprnum]; blndprsub++)
        {
            char currLayer = scanline->prioritySortedBgs[blndprnum][blndprsub];
            if (getAlphaBit( scanline->layers[currLayer][pixelpos] ) == 1 && gpu.blendControl & ( 1 << (8 + currLayer)) && isbgEnabled(currLayer))
            {
                *colorOutput = scanline->layers[currLayer][pixelpos];
                return true;
            }
            //if we hit a non target layer we should bail
            if ( getAlphaBit( scanline->layers[currLayer][pixelpos] ) == 1 && isbgEnabled(currLayer) && prnum != blndprnum )
            {
                return false;
            }
        }
        prsub = 0; //start from zero in the next iteration
    }
    //no background got hit, check if backdrop is enabled and return it if enabled otherwise fail
    if (gpu.blendControl & BLDCNT_TGT2_BD)
    {
        *colorOutput = *(uint16_t*)gpu.palette;
        return true;
    }
    else
    {
        return false;
    }
}

#define WINMASK_BG0    (1 << 0)
#define WINMASK_BG1    (1 << 1)
#define WINMASK_BG2    (1 << 2)
#define WINMASK_BG3    (1 << 3)
#define WINMASK_OBJ    (1 << 4)
#define WINMASK_CLR    (1 << 5)
#define WINMASK_WINOUT (1 << 6)

//checks if window horizontal is in bounds and takes account WIN wraparound
static bool winCheckHorizontalBounds(u16 left, u16 right, u16 xpos)
{
    if (left > right)
        return (xpos >= left || xpos < right);
    else
        return (xpos >= left && xpos < right);
}

// Parts of this code heavily borrowed from NanoboyAdvance.
static void DrawSprites(struct scanlineData* scanline, uint16_t vcount, bool windowsEnabled)
{
    int i;
    unsigned int x;
    unsigned int y;
    void *objtiles = gpu.spriteGfxData;
    unsigned int blendMode = (gpu.blendControl >> 6) & 3;
    bool winShouldBlendPixel = true;

    int16_t matrix[2][2] = {};

    if (!(gpu.displayControl & (1 << 6)))
    {
        puts("2-D OBJ Character mapping not supported.");
    }

    for (i = MAX_OAM_SPRITES - 1; i >= 0; i--)
    {
        struct OamData *oam = &gpu.spriteList[i];
        unsigned int width;
        unsigned int height;
        uint16_t *pixels;

        bool isAffine  = oam->affineMode & 1;
        bool doubleSizeOrDisabled = (oam->affineMode >> 1) & 1;
        bool isSemiTransparent = (oam->objMode == 1);
        bool isObjWin = (oam->objMode == 2);

        if (!(isAffine) && doubleSizeOrDisabled) // disable for non-affine
        {
            continue;
        }

        if (oam->shape == 0)
        {
            width = (1 << oam->size) * 8;
            height = (1 << oam->size) * 8;
        }
        else if (oam->shape == 1) // wide
        {
            width = spriteSizes[oam->size][1];
            height = spriteSizes[oam->size][0];
        }
        else if (oam->shape == 2) // tall
        {
            width = spriteSizes[oam->size][0];
            height = spriteSizes[oam->size][1];
        }
        else
        {
            continue; // prohibited, do not draw
        }

        int rect_width = width;
        int rect_height = height;

        int half_width = width / 2;
        int half_height = height / 2;

        pixels = scanline->spriteLayers[oam->priority];

        int32_t x = oam->x;
        int32_t y = oam->y;

        if (x >= displayWidth)
            x -= 512;
        if (y >= displayHeight)
            y -= 256;

        if (isAffine)
        {
            //TODO: there is probably a better way to do this
            u8 matrixNum = oam->matrixNum * 4;

            struct OamData *oam1 = &(gpu.spriteList[matrixNum]);
            struct OamData *oam2 = &(gpu.spriteList[matrixNum + 1]);
            struct OamData *oam3 = &(gpu.spriteList[matrixNum + 2]);
            struct OamData *oam4 = &(gpu.spriteList[matrixNum + 3]);

            matrix[0][0] = oam1->affineParam;
            matrix[0][1] = oam2->affineParam;
            matrix[1][0] = oam3->affineParam;
            matrix[1][1] = oam4->affineParam;

            if (doubleSizeOrDisabled) // double size for affine
            {
                rect_width *= 2;
                rect_height *= 2;
                half_width *= 2;
                half_height *= 2;
            }
        }
        else
        {
            // Identity
            matrix[0][0] = 0x100;
            matrix[0][1] = 0;
            matrix[1][0] = 0;
            matrix[1][1] = 0x100;
        }

        x += half_width;
        y += half_height;

        // Does this sprite actually draw on this scanline?
        if (vcount >= (y - half_height) && vcount < (y + half_height))
        {
            int local_y = (oam->mosaic == 1) ? applySpriteVerticalMosaicEffect(vcount) - y : vcount - y;
            int number  = oam->tileNum;
            int palette = oam->paletteNum;
            bool flipX  = !isAffine && ((oam->matrixNum >> 3) & 1);
            bool flipY  = !isAffine && ((oam->matrixNum >> 4) & 1);
            bool is8BPP  = oam->bpp & 1;

            for (int local_x = -half_width; local_x <= half_width; local_x++)
            {
                uint8_t *tiledata = (uint8_t *)objtiles;
                uint16_t *palette = OBJ_PLTT;
                int local_mosaicX;
                int tex_x;
                int tex_y;

                unsigned int global_x = local_x + x;

                if (global_x < 0 || global_x >= displayWidth)
                    continue;

                if (oam->mosaic == 1)
                {
                    //mosaic effect has to be applied to global coordinates otherwise the mosaic will scroll
                    local_mosaicX = applySpriteHorizontalMosaicEffect(global_x) - x;
                    tex_x = ((matrix[0][0] * local_mosaicX + matrix[0][1] * local_y) >> 8) + (width / 2);
                    tex_y = ((matrix[1][0] * local_mosaicX + matrix[1][1] * local_y) >> 8) + (height / 2);
                }else{
                    tex_x = ((matrix[0][0] * local_x + matrix[0][1] * local_y) >> 8) + (width / 2);
                    tex_y = ((matrix[1][0] * local_x + matrix[1][1] * local_y) >> 8) + (height / 2);
                }

                /* Check if transformed coordinates are inside bounds. */

                if (tex_x >= width || tex_y >= height || tex_x < 0 || tex_y < 0)
                    continue;

                if (flipX)
                    tex_x = width  - tex_x - 1;
                if (flipY)
                    tex_y = height - tex_y - 1;

                int tile_x = tex_x % 8;
                int tile_y = tex_y % 8;
                int block_x = tex_x / 8;
                int block_y = tex_y / 8;
                int block_offset = ((block_y * (gpu.displayControl & 0x40 ? (width / 8) : 16)) + block_x);
                uint16_t pixel = 0;

                if (!is8BPP)
                {
                    pixel = tiledata[(block_offset + oam->tileNum) * 32 + (tile_y * 4) + (tile_x / 2)];
                    if (tile_x & 1)
                        pixel >>= 4;
                    else
                        pixel &= 0xF;
                    palette += oam->paletteNum * 16;
                }
                else
                {
                    pixel = tiledata[(block_offset * 2 + oam->tileNum) * 32 + (tile_y * 8) + tile_x];
                }

                if (pixel != 0)
                {
                    uint16_t color = palette[pixel];;

                    //if sprite mode is 2 then write to the window mask instead
                    if (isObjWin)
                    {
                        if (scanline->winMask[global_x] & WINMASK_WINOUT)
                            scanline->winMask[global_x] = (gpu.window.out >> 8) & 0x3F;
                        continue;
                    }
                    //this code runs if pixel is to be drawn
                    if (global_x < displayWidth && global_x >= 0)
                    {
                        //check if its enabled in the window (if window is enabled)
                        winShouldBlendPixel = (windowsEnabled == false || scanline->winMask[global_x] & WINMASK_CLR);
                        
                        //has to be separated from the blend mode switch statement because of OBJ semi transparancy feature
                        if ((blendMode == 1 && gpu.blendControl & BLDCNT_TGT1_OBJ && winShouldBlendPixel) || isSemiTransparent)
                        {
                            uint16_t targetA = color;
                            uint16_t targetB = 0;
                            if (alphaBlendSelectTargetB(scanline, &targetB, oam->priority, 0, global_x, false))
                            {
                                color = alphaBlendColor(targetA, targetB);
                            }
                        }
                        else if (gpu.blendControl & BLDCNT_TGT1_OBJ && winShouldBlendPixel)
                        {
                            switch (blendMode)
                            {
                            case 2:
                                color = alphaBrightnessIncrease(color);
                                break;
                            case 3:
                                color = alphaBrightnessDecrease(color);
                                break;
                            }
                        }
                        
                        //write pixel to pixel framebuffer
                        pixels[global_x] = color | (1 << 15);
                    }
                }
            }
        }
    }
}

static void GetWindowCoords(u8 which, uint16_t *bottom, uint16_t *top, uint16_t *right, uint16_t *left)
{
    *bottom = (gpu.window.state[which].y & 0xFFFF); //y2;
    *top = (gpu.window.state[which].y & 0xFFFF0000) >> 16; //y1;
    *right = (gpu.window.state[which].x & 0xFFFF); //x2
    *left = (gpu.window.state[which].x & 0xFFFF0000) >> 16; //x1
}

static void DrawScanline(uint16_t *pixels, uint16_t vcount)
{
    unsigned int mode = gpu.displayControl & 3;
    unsigned char numOfBgs = (mode == 0 ? 4 : 3);
    int bgnum, prnum;
    struct scanlineData scanline;
    unsigned int blendMode = (gpu.blendControl >> 6) & 3;
    unsigned int xpos;

    //initialize all priority bookkeeping data
    memset(scanline.layers, 0, sizeof(scanline.layers));
    memset(scanline.winMask, 0, sizeof(scanline.winMask));
    memset(scanline.spriteLayers, 0, sizeof(scanline.spriteLayers));
    memset(scanline.prioritySortedBgsCount, 0, sizeof(scanline.prioritySortedBgsCount));

    for (bgnum = 0; bgnum < numOfBgs; bgnum++)
    {
        uint16_t priority = gpu.bg[bgnum].priority;

        scanline.bgtoprio[bgnum] = priority;

        char priorityCount = scanline.prioritySortedBgsCount[priority];
        scanline.prioritySortedBgs[priority][priorityCount] = bgnum;
        scanline.prioritySortedBgsCount[priority]++;
    }
    
    switch (mode)
    {
    case 0:
        // All backgrounds are text mode
        for (bgnum = 3; bgnum >= 0; bgnum--)
        {
            if (isbgEnabled(bgnum) && layerEnabled[bgnum])
            {
                uint16_t bghoffs = gpu.bg[bgnum].x;
                uint16_t bgvoffs = gpu.bg[bgnum].y;

                RenderBGScanline(bgnum, bghoffs, bgvoffs, vcount, scanline.layers[bgnum]);
            }
        }
        
        break;
    case 1:
        // BG2 is affine
        bgnum = 2;
        if (isbgEnabled(bgnum) && layerEnabled[bgnum])
        {
            RenderRotScaleBGScanline(bgnum, gpu.affineBg2.x, gpu.affineBg2.y, vcount, scanline.layers[bgnum]);
        }
        // BG0 and BG1 are text mode
        for (bgnum = 1; bgnum >= 0; bgnum--)
        {
            if (isbgEnabled(bgnum) && layerEnabled[bgnum])
            {
                uint16_t bghoffs = gpu.bg[bgnum].x;
                uint16_t bgvoffs = gpu.bg[bgnum].y;

                RenderBGScanline(bgnum, bghoffs, bgvoffs, vcount, scanline.layers[bgnum]);
            }
        }
        break;
    default:
        // printf("Video mode %u is unsupported.\n", mode);
        break;
    }
    
    bool windowsEnabled = false;
    uint16_t WIN0bottom, WIN0top, WIN0right, WIN0left;
    uint16_t WIN1bottom, WIN1top, WIN1right, WIN1left;
    bool WIN0enable, WIN1enable;
    WIN0enable = false;
    WIN1enable = false;

    //figure out if WIN0 masks on this scanline
    if (gpu.displayControl & DISPCNT_WIN0_ON)
    {
        //acquire the window coordinates
        GetWindowCoords(0, &WIN0bottom, &WIN0top, &WIN0right, &WIN0left);

        //figure out WIN Y wraparound and check bounds accordingly
        if (WIN0top > WIN0bottom) {
            if (vcount >= WIN0top || vcount < WIN0bottom)
                WIN0enable = true;
        } else {
            if (vcount >= WIN0top && vcount < WIN0bottom)
                WIN0enable = true;
        }
        
        windowsEnabled = true;
    }
    //figure out if WIN1 masks on this scanline
    if (gpu.displayControl & DISPCNT_WIN1_ON)
    {
        GetWindowCoords(1, &WIN1bottom, &WIN1top, &WIN1right, &WIN1left);
        
        if (WIN1top > WIN1bottom) {
            if (vcount >= WIN1top || vcount < WIN1bottom)
                WIN1enable = true;
        } else {
            if (vcount >= WIN1top && vcount < WIN1bottom)
                WIN1enable = true;
        }
        
        windowsEnabled = true;
    }
    //enable windows if OBJwin is enabled
    if (gpu.displayControl & DISPCNT_OBJWIN_ON && gpu.displayControl & DISPCNT_OBJ_ON)
    {
        windowsEnabled = true;
    }
    
    //draw to pixel mask
    if (windowsEnabled)
    {
        for (xpos = 0; xpos < displayWidth; xpos++)
        {
            //win0 checks
            if (WIN0enable && winCheckHorizontalBounds(WIN0left, WIN0right, xpos))
                scanline.winMask[xpos] = gpu.window.in & 0x3F;
            //win1 checks
            else if (WIN1enable && winCheckHorizontalBounds(WIN1left, WIN1right, xpos))
                scanline.winMask[xpos] = (gpu.window.in >> 8) & 0x3F;
            else
                scanline.winMask[xpos] = (gpu.window.out & 0x3F) | WINMASK_WINOUT;
        }
    }

    if (gpu.displayControl & DISPCNT_OBJ_ON && layerEnabled[4])
        DrawSprites(&scanline, vcount, windowsEnabled);

    //iterate trough every priority in order
    for (prnum = 3; prnum >= 0; prnum--)
    {
        for (char prsub = scanline.prioritySortedBgsCount[prnum] - 1; prsub >= 0; prsub--)
        {
            char bgnum = scanline.prioritySortedBgs[prnum][prsub];
            //if background is enabled then draw it
            if (isbgEnabled(bgnum))
            {
                int lineStart, lineEnd;
                GetBGScanlinePos(bgnum, &lineStart, &lineEnd);
                uint16_t *src = scanline.layers[bgnum];
                //copy all pixels to framebuffer 
                for (xpos = lineStart; xpos < lineEnd; xpos++)
                {
                    uint16_t color = src[xpos];
                    if (!getAlphaBit(color))
                        continue; //do nothing if alpha bit is not set

                    bool winEffectEnable = true;

                    if (windowsEnabled)
                    {
                        int mask = scanline.winMask[xpos - lineStart];
                        winEffectEnable = ((mask & WINMASK_CLR) >> 5);
                        //if bg is disabled inside the window then do not draw the pixel
                        if ( !(mask & 1 << bgnum) )
                            continue;
                    }
                    
                    //blending code
                    if (blendMode != 0 && gpu.blendControl & (1 << bgnum) && winEffectEnable)
                    {
                        uint16_t targetA = color;
                        uint16_t targetB = 0;
                        bool isSpriteBlendingEnabled = false;
                        
                        switch (blendMode)
                        {
                        case 1:
                            isSpriteBlendingEnabled = gpu.blendControl & BLDCNT_TGT2_OBJ ? true : false;
                            //find targetB and blend it
                            if (alphaBlendSelectTargetB(&scanline, &targetB, prnum, prsub+1, xpos, isSpriteBlendingEnabled))
                            {
                                color = alphaBlendColor(targetA, targetB);
                            }
                            break;
                        case 2:
                            color = alphaBrightnessIncrease(targetA);
                            break;
                        case 3:
                            color = alphaBrightnessDecrease(targetA);
                            break;
                        }
                    }
                    //write the pixel to scanline buffer output
                    pixels[xpos] = color;
                }
            }
        }
        //draw sprites on current priority
        uint16_t *src = scanline.spriteLayers[prnum];
        for (xpos = 0; xpos < displayWidth; xpos++)
        {
            if (getAlphaBit(src[xpos]))
            {
                //check if sprite pixel draws inside window
                if (windowsEnabled && !(scanline.winMask[xpos] & WINMASK_OBJ))
                    continue;
                //draw the pixel
                pixels[xpos] = src[xpos];
            }
        }
    }
}

static void DrawFrame(uint16_t *pixels)
{
    u32 i;
    u32 j;
    static uint16_t scanlines[DISPLAY_HEIGHT][DISPLAY_WIDTH];
    unsigned int blendMode = (gpu.blendControl >> 6) & 3;
    uint16_t backdropColor = *(uint16_t *)gpu.palette;

    // backdrop color brightness effects
    if (gpu.blendControl & BLDCNT_TGT1_BD)
    {
        switch (blendMode)
        {
        case 2:
            backdropColor = alphaBrightnessIncrease(backdropColor);
            break;
        case 3:
            backdropColor = alphaBrightnessDecrease(backdropColor);
            break;
        }
    }

    for (i = 0; i < displayHeight; i++)
    {
        // Clear this scanline
        for (u32 j = 0; j < displayWidth; j++)
            scanlines[i][j] = backdropColor;

        gpu.vCount = i;

        DrawScanline(scanlines[i], i);

        gpu.displayStatus |= INTR_FLAG_HBLANK;

        if (gpu.scanlineEffect.type != GPU_SCANLINE_EFFECT_OFF)
            RunScanlineEffect();

        if (runHBlank && (gpu.displayStatus & DISPSTAT_HBLANK_INTR))
            DoHBlankUpdate();

        gpu.displayStatus &= ~INTR_FLAG_HBLANK;
    }

    // Copy to screen
    for (i = 0; i < displayHeight; i++)
        memcpy(&pixels[i * displayWidth], scanlines[i], displayWidth * sizeof(u16));
}

static void RunFrame(void)
{
#ifndef USE_THREAD
    GameLoop();
#endif

    gpu.displayStatus |= INTR_FLAG_VBLANK;

    if (runVBlank && (gpu.displayStatus & DISPSTAT_VBLANK_INTR))
        FrameUpdate();
}

static void RunScanlineEffect(void)
{
    GpuRefreshScanlineEffect();

    if (gpu.scanlineEffect.position < displayHeight - 1)
        gpu.scanlineEffect.position++;
}

void RenderFrame(SDL_Texture *texture)
{
    int pitch = displayWidth * sizeof (Uint16);

#ifdef USE_TEXTURE_LOCK
    int *pixels = NULL;

    SDL_LockTexture(texture, NULL, (void **)&pixels, &pitch);

    DrawFrame((uint16_t *)pixels);

    SDL_UnlockTexture(texture);
#else
    static uint16_t image[DISPLAY_WIDTH * DISPLAY_HEIGHT];

    DrawFrame(image);

    SDL_UpdateTexture(texture, NULL, image, pitch);
#endif

    gpu.vCount = displayHeight + 1; // prep for being in VBlank period
}

#ifdef USE_THREAD
int DoMain(void *data)
{
    while (TRUE)
        GameLoop();
}
#endif

void AudioUpdate(void)
{
    if (gSoundInit == FALSE)
        return;

    gPcmDmaCounter = gSoundInfo.pcmDmaCounter;

    m4aSoundMain();

    m4aSoundVSync();
}

void VBlankIntrWait(void)
{
#ifdef USE_THREAD
    SDL_AtomicSet(&isFrameAvailable, 1);
    SDL_SemWait(vBlankSemaphore);
#endif
}

u8 BinToBcd(u8 bin)
{
    int placeCounter = 1;
    u8 out = 0;
    do
    {
        out |= (bin % 10) * placeCounter;
        placeCounter *= 16;
    }
    while ((bin /= 10) > 0);

    return out;
}

void Platform_GetStatus(struct SiiRtcInfo *rtc)
{
    rtc->status = internalClock.status;
}

void Platform_SetStatus(struct SiiRtcInfo *rtc)
{
    internalClock.status = rtc->status;
}

static void UpdateInternalClock(void)
{
    time_t rawTime = time(NULL);
    struct tm *time = localtime(&rawTime);

    internalClock.year = BinToBcd(time->tm_year - 100);
    internalClock.month = BinToBcd(time->tm_mon) + 1;
    internalClock.day = BinToBcd(time->tm_mday);
    internalClock.dayOfWeek = BinToBcd(time->tm_wday);
    internalClock.hour = BinToBcd(time->tm_hour);
    internalClock.minute = BinToBcd(time->tm_min);
    internalClock.second = BinToBcd(time->tm_sec);
}

void Platform_GetDateTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->year = internalClock.year;
    rtc->month = internalClock.month;
    rtc->day = internalClock.day;
    rtc->dayOfWeek = internalClock.dayOfWeek;
    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    printf("GetDateTime: %d-%02d-%02d %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->year),
                                                         ConvertBcdToBinary(rtc->month),
                                                         ConvertBcdToBinary(rtc->day),
                                                         ConvertBcdToBinary(rtc->hour),
                                                         ConvertBcdToBinary(rtc->minute),
                                                         ConvertBcdToBinary(rtc->second));
}

void Platform_SetDateTime(struct SiiRtcInfo *rtc)
{
    internalClock.month = rtc->month;
    internalClock.day = rtc->day;
    internalClock.dayOfWeek = rtc->dayOfWeek;
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_GetTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    printf("GetTime: %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->hour),
                                        ConvertBcdToBinary(rtc->minute),
                                        ConvertBcdToBinary(rtc->second));
}

void Platform_SetTime(struct SiiRtcInfo *rtc)
{
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_SetAlarm(u8 *alarmData)
{
    // TODO
}

// Following functions taken from mGBA's source
u16 ArcTan(s16 i)
{
    s32 a = -((i * i) >> 14);
    s32 b = ((0xA9 * a) >> 14) + 0x390;
    b = ((b * a) >> 14) + 0x91C;
    b = ((b * a) >> 14) + 0xFB6;
    b = ((b * a) >> 14) + 0x16AA;
    b = ((b * a) >> 14) + 0x2081;
    b = ((b * a) >> 14) + 0x3651;
    b = ((b * a) >> 14) + 0xA2F9;

    return (i * b) >> 16;
}

u16 ArcTan2(s16 x, s16 y)
{
    if (!y)
    {
        if (x >= 0)
            return 0;
        return 0x8000;
    }
    if (!x)
    {
        if (y >= 0)
            return 0x4000;
        return 0xC000;
    }
    if (y >= 0)
    {
        if (x >= 0)
        {
            if (x >= y)
                return ArcTan((y << 14) / x);
        }
        else if (-x >= y)
            return ArcTan((y << 14) / x) + 0x8000;
        return 0x4000 - ArcTan((x << 14) / y);
    }
    else
    {
        if (x <= 0)
        {
            if (-x > -y)
                return ArcTan((y << 14) / x) + 0x8000;
        }
        else if (x >= -y)
            return ArcTan((y << 14) / x) + 0x10000;
        return 0xC000 - ArcTan((x << 14) / y);
    }
}

u16 Sqrt(u32 num)
{
    if (!num)
        return 0;
    u32 lower;
    u32 upper = num;
    u32 bound = 1;
    while (bound < upper)
    {
        upper >>= 1;
        bound <<= 1;
    }
    while (1)
    {
        upper = num;
        u32 accum = 0;
        lower = bound;
        while (1)
        {
            u32 oldLower = lower;
            if (lower <= upper >> 1)
                lower <<= 1;
            if (oldLower >= upper >> 1)
                break;
        }
        while (1)
        {
            accum <<= 1;
            if (upper >= lower)
            {
                ++accum;
                upper -= lower;
            }
            if (lower == bound)
                break;
            lower >>= 1;
        }
        u32 oldBound = bound;
        bound += accum;
        bound >>= 1;
        if (bound >= oldBound)
        {
            bound = oldBound;
            break;
        }
    }
    return bound;
}
