#ifndef _CHASM_HUD_H_
#define _CHASM_HUD_H_

#include <stdint.h>
#include "UIOverlay.h"

/*
    The game's own UI colours - the overlay's, in every build (not the debug panels'). Shared by the
    date and totals (ApplicationChasmTime.cpp) and play mode's cards and pins (ApplicationChasmSelect.cpp),
    so they read as one interface.
*/
#define HUD_PANEL       UIColor( 22, 28, 32,178)    //dark and see-through, so the map shows behind it
#define HUD_TEXT        UIColor(238,236,226,255)
#define HUD_TEXT_DIM    UIColor(238,236,226,150)
#define HUD_MARK        UIColor(255,255,255,255)
#define HUD_AMBER       UIColor(236,202,92,255)     //what wants attention: a site, someone without a house
#define HUD_SELECT      UIColor(255,158,40,255)     //Blender's selection orange: the selection's own colour

inline uint32_t HudFaded(uint32_t c, uint8_t alpha){
    return (c & 0x00FFFFFFu) | ((uint32_t)alpha << 24);
}

#endif
