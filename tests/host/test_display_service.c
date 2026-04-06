#include "display_service.h"

#include <assert.h>
#include <stddef.h>

int main(void)
{
    const DisplayServicePanelInfo_t *info = DisplayService_GetPanelInfo();

    assert(info != NULL);
    assert(info->width_px > 0U);
    assert(info->height_px > 0U);
    assert(info->stride_bytes == ((size_t)info->width_px + 7U) / 8U);
    assert(info->mono_buffer_bytes == info->stride_bytes * (size_t)info->height_px);

#if OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_2IN13_V4
    assert(info->width_px == 122U);
    assert(info->height_px == 250U);
    assert(info->mono_buffer_bytes == 4000U);
    assert(info->has_internal_accent_plane == false);
#elif OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83_V2
    assert(info->width_px == 648U);
    assert(info->height_px == 480U);
    assert(info->mono_buffer_bytes == 38880U);
    assert(info->has_internal_accent_plane == false);
#elif OPENPERIPH_EPD_PANEL == OPENPERIPH_EPD_PANEL_5IN83B_V2
    assert(info->width_px == 648U);
    assert(info->height_px == 480U);
    assert(info->mono_buffer_bytes == 38880U);
    assert(info->has_internal_accent_plane == true);
#else
#error Unsupported OPENPERIPH_EPD_PANEL selection in host test.
#endif

    return 0;
}
