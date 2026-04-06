#ifndef DISPLAY_SERVICE_H
#define DISPLAY_SERVICE_H

#include "app_protocol.h"
#include "openperiph_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DISPLAY_SERVICE_FONT_8 = 0,
    DISPLAY_SERVICE_FONT_12,
    DISPLAY_SERVICE_FONT_16,
    DISPLAY_SERVICE_FONT_20,
    DISPLAY_SERVICE_FONT_24,
} DisplayServiceFont_t;

typedef struct {
    uint16_t width_px;
    uint16_t height_px;
    size_t stride_bytes;
    size_t mono_buffer_bytes;
    bool has_internal_accent_plane;
} DisplayServicePanelInfo_t;

const DisplayServicePanelInfo_t *DisplayService_GetPanelInfo(void);
bool DisplayService_Init(void);
bool DisplayService_Clear(bool full_refresh);
bool DisplayService_DrawText(const AppDrawTextCommand_t *cmd);
bool DisplayService_RenderText(uint16_t x,
                               uint16_t y,
                               DisplayServiceFont_t font,
                               const char *text,
                               bool clear_first,
                               bool full_refresh);
void DisplayService_Sleep(void);

#endif
