#include "display_service.h"

#include "openperiph_config.h"

#include <string.h>

static const DisplayServicePanelInfo_t kPanelInfo = {
    .width_px = OPENPERIPH_EPD_WIDTH_PX,
    .height_px = OPENPERIPH_EPD_HEIGHT_PX,
    .stride_bytes = OPENPERIPH_EPD_STRIDE_BYTES,
    .mono_buffer_bytes = OPENPERIPH_EPD_MONO_BUFFER_BYTES,
    .has_internal_accent_plane = (OPENPERIPH_EPD_HAS_ACCENT_PLANE != 0U),
};

#ifdef OPENPERIPH_HOST_TEST

const DisplayServicePanelInfo_t *DisplayService_GetPanelInfo(void)
{
    return &kPanelInfo;
}

bool DisplayService_Init(void)
{
    return true;
}

bool DisplayService_Clear(bool full_refresh)
{
    (void)full_refresh;
    return true;
}

bool DisplayService_DrawText(const AppDrawTextCommand_t *cmd)
{
    (void)cmd;
    return true;
}

bool DisplayService_RenderText(uint16_t x,
                               uint16_t y,
                               DisplayServiceFont_t font,
                               const char *text,
                               bool clear_first,
                               bool full_refresh)
{
    (void)x;
    (void)y;
    (void)font;
    (void)text;
    (void)clear_first;
    (void)full_refresh;
    return true;
}

void DisplayService_Sleep(void)
{
}

#else

#include "DEV_Config.h"
#include "EPD_2in13_V4.h"
#include "EPD_5in83_V2.h"
#include "EPD_5in83b_V2.h"
#include "GUI_Paint.h"
#include "fonts.h"

static uint8_t s_mono_buffer[OPENPERIPH_EPD_MONO_BUFFER_BYTES];
#if OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83B_V2
static uint8_t s_accent_buffer[OPENPERIPH_EPD_MONO_BUFFER_BYTES];
#endif
static bool s_initialized = false;

static sFONT *DisplayService_SelectFont(DisplayServiceFont_t font)
{
    switch (font) {
    case DISPLAY_SERVICE_FONT_8:
        return &Font8;
    case DISPLAY_SERVICE_FONT_12:
        return &Font12;
    case DISPLAY_SERVICE_FONT_16:
        return &Font16;
    case DISPLAY_SERVICE_FONT_20:
        return &Font20;
    case DISPLAY_SERVICE_FONT_24:
        return &Font24;
    default:
        return &Font12;
    }
}

static bool DisplayService_WillTextFit(uint16_t x,
                                       uint16_t y,
                                       const sFONT *font,
                                       size_t text_len)
{
    size_t max_chars = 0U;

    if ((font == NULL) || (text_len == 0U)) {
        return false;
    }

    if ((x >= kPanelInfo.width_px) || (y >= kPanelInfo.height_px)) {
        return false;
    }

    if (((uint32_t)y + font->Height) > kPanelInfo.height_px) {
        return false;
    }

    max_chars = (kPanelInfo.width_px - x) / font->Width;
    return (max_chars > 0U) && (text_len <= max_chars);
}

static uint8_t *DisplayService_MonoBuffer(void)
{
    return s_mono_buffer;
}

static void DisplayService_CopyCountedText(const AppDrawTextCommand_t *cmd,
                                           char *out_text,
                                           size_t out_capacity)
{
    size_t copy_len = 0U;

    if ((cmd == NULL) || (out_text == NULL) || (out_capacity == 0U)) {
        return;
    }

    copy_len = cmd->text_len;
    if (copy_len >= out_capacity) {
        copy_len = out_capacity - 1U;
    }
    memcpy(out_text, cmd->text, copy_len);
    out_text[copy_len] = '\0';
}

static void DisplayService_ResetBuffersToWhite(void)
{
    memset(DisplayService_MonoBuffer(), 0xFF, kPanelInfo.mono_buffer_bytes);
#if OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83B_V2
    memset(s_accent_buffer, 0xFF, kPanelInfo.mono_buffer_bytes);
#endif
}

static bool DisplayService_Present(bool full_refresh)
{
#if OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_2IN13_V4
    if (full_refresh) {
        EPD_2in13_V4_Display_Base(DisplayService_MonoBuffer());
    } else {
        EPD_2in13_V4_Display_Fast(DisplayService_MonoBuffer());
    }
#elif OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83_V2
    (void)full_refresh;
    EPD_5in83_V2_Display(DisplayService_MonoBuffer());
#elif OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83B_V2
    (void)full_refresh;
    EPD_5IN83B_V2_Display(DisplayService_MonoBuffer(), s_accent_buffer);
#endif
    return true;
}

const DisplayServicePanelInfo_t *DisplayService_GetPanelInfo(void)
{
    return &kPanelInfo;
}

bool DisplayService_Init(void)
{
    if (s_initialized) {
        return true;
    }

    if (DEV_Module_Init() != 0) {
        return false;
    }

#if OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_2IN13_V4
    EPD_2in13_V4_Init();
    EPD_2in13_V4_Clear();
#elif OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83_V2
    EPD_5in83_V2_Init();
#elif OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83B_V2
    if (EPD_5IN83B_V2_Init() != 0U) {
        return false;
    }
#endif

    DisplayService_ResetBuffersToWhite();
    Paint_NewImage(DisplayService_MonoBuffer(),
                   kPanelInfo.width_px,
                   kPanelInfo.height_px,
                   ROTATE_0,
                   WHITE);
    Paint_SelectImage(DisplayService_MonoBuffer());
    Paint_Clear(WHITE);
    s_initialized = true;
    return true;
}

bool DisplayService_Clear(bool full_refresh)
{
    if (!s_initialized && !DisplayService_Init()) {
        return false;
    }

    Paint_SelectImage(DisplayService_MonoBuffer());
    Paint_Clear(WHITE);
    DisplayService_ResetBuffersToWhite();
    return DisplayService_Present(full_refresh);
}

bool DisplayService_RenderText(uint16_t x,
                               uint16_t y,
                               DisplayServiceFont_t font,
                               const char *text,
                               bool clear_first,
                               bool full_refresh)
{
    sFONT *selected_font = NULL;

    if ((text == NULL) || (text[0] == '\0')) {
        return false;
    }

    if (!s_initialized && !DisplayService_Init()) {
        return false;
    }

    selected_font = DisplayService_SelectFont(font);
    if (!DisplayService_WillTextFit(x, y, selected_font, strlen(text))) {
        return false;
    }

    Paint_SelectImage(DisplayService_MonoBuffer());
    if (clear_first) {
        Paint_Clear(WHITE);
        DisplayService_ResetBuffersToWhite();
    }

    Paint_DrawString_EN(x, y, text, selected_font, BLACK, WHITE);
    return DisplayService_Present(full_refresh);
}

bool DisplayService_DrawText(const AppDrawTextCommand_t *cmd)
{
    DisplayServiceFont_t font = DISPLAY_SERVICE_FONT_12;
    char text[APP_TEXT_MAX_LEN + 1U] = {0};

    if ((cmd == NULL) || (cmd->text_len == 0U) || (cmd->text_len > APP_TEXT_MAX_LEN)) {
        return false;
    }

    switch (cmd->font_id) {
    case APP_FONT_16:
        font = DISPLAY_SERVICE_FONT_16;
        break;
    case APP_FONT_12:
    default:
        font = DISPLAY_SERVICE_FONT_12;
        break;
    }

    DisplayService_CopyCountedText(cmd, text, sizeof(text));
    return DisplayService_RenderText(cmd->x,
                                     cmd->y,
                                     font,
                                     text,
                                     (cmd->flags & APP_DRAW_FLAG_CLEAR_FIRST) != 0U,
                                     (cmd->flags & APP_DRAW_FLAG_FULL_REFRESH) != 0U);
}

void DisplayService_Sleep(void)
{
    if (!s_initialized) {
        return;
    }

#if OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_2IN13_V4
    EPD_2in13_V4_Sleep();
#elif OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83_V2
    EPD_5in83_V2_Sleep();
#elif OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83B_V2
    EPD_5IN83B_V2_Sleep();
#endif

    DEV_Module_Exit();
    s_initialized = false;
}

#endif
