#ifndef __AML_PSDK_RWRGBA_H
#define __AML_PSDK_RWRGBA_H

#include <plugin-sdk/gta_base/RGBA.h>
#include <plugin-sdk/renderware/RwVector.h>
#include <plugin-sdk/sdk_base.h>

#define RwRGBA CRGBA

struct RwRGBAReal {
    RwReal red;
    RwReal green;
    RwReal blue;
    RwReal alpha;
};

DECL_FASTCALL_SIMPLE_GLO(RwRGBAGammaCorrect, _Z18RwRGBAGammaCorrectP6RwRGBA, RwRGBA *, RwRGBA *color);
DECL_FASTCALL_SIMPLE_GLO(RwRGBAToPixel, _Z13RwRGBAToPixelP6RwRGBAi, RwUInt32, RwRGBA *color, RwInt32 format);
DECL_FASTCALL_SIMPLE_GLO(RwRGBASetFromPixel, _Z18RwRGBASetFromPixelP6RwRGBAji, RwRGBA *, RwRGBA *color, RwUInt32 pixel,
                         RwInt32 format);
#endif // __AML_PSDK_RWRGBA_H
