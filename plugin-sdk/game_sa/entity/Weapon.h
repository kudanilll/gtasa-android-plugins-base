#ifndef __AML_PSDK_SAWEAPON_H
#define __AML_PSDK_SAWEAPON_H

#include <plugin-sdk/game_sa/plugin.h>
#include <plugin-sdk/game_sa/enum/WeaponType.h>

struct FxSystem_c;

DECL_CLASS(CWeapon)
    eWeaponType m_eWeaponType;
    eWeaponState m_nState;
    int m_nAmmoInClip;
    int m_nAmmoTotal;
    unsigned int m_nTimeForNextShot;
    bool m_bFirstPersonWeaponModeSelected;
    bool m_bNoModel;
    FxSystem_c *m_pFxSystem;
DECL_CLASS_END()
CHECKSIZE(CWeapon, 0x1C, 0x20);

#endif // __AML_PSDK_SAWEAPON_H