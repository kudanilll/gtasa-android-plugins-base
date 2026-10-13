#ifndef __AML_PSDK_SAAUDIOENGINE_H
#define __AML_PSDK_SAAUDIOENGINE_H

#include <plugin-sdk/game_sa/audio/AECollisionAudioEntity.h>
#include <plugin-sdk/game_sa/audio/AEFrontendAudioEntity.h>
#include <plugin-sdk/game_sa/audio/AEPedlessSpeechAudioEntity.h>
#include <plugin-sdk/game_sa/audio/AEScriptAudioEntity.h>
#include <plugin-sdk/game_sa/plugin.h>

struct CAEGlobalWeaponAudioEntity;

struct tBeatInfoBeat {
    int Time;
    int Type;
};

struct tBeatInfo {
    tBeatInfoBeat BeatWindow[20];
    int bBeatInfoPresent;
    int BeatTypeThisFrame;
    int BeatNumber;
};

DECL_CLASS(CAudioEngine)
// Member values
unsigned char m_bMissionCompleteActive;
unsigned char m_bStoppingMissionComplete;
signed char m_nCurrentRadiostationId;
int m_nAudioStreamClientHandle;
tBeatInfo m_BeatInfo;
CAEFrontendAudioEntity m_FrontendAudio;
CAEScriptAudioEntity m_scriptAudio;
CAECollisionAudioEntity m_collisionAudio;
CAEGlobalWeaponAudioEntity *m_pWeaponAudio;
CAEPedlessSpeechAudioEntity m_pedlessSpeechAudio;
CAEDoorAudioEntity m_doorAudio;
DECL_CLASS_END()
CHECKSIZE(CAudioEngine, 0x1FD8, 0x2F50);

DECL_OBJECT_PLT_GLOBAL(CAudioEngine, AudioEngine, BYBIT(0x675F74, 0x849F58));
#define AudioEngine AudioEngine()

#endif // __AML_PSDK_SAAUDIOENGINE_H