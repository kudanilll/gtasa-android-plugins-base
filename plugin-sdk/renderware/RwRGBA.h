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

#endif // __AML_PSDK_RWRGBA_H
