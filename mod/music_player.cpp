#include "music_player.h"

#include <mod/amlmod.h>
#include <mod/logger.h>
#include <plugin-sdk/game_sa/Events.h>
#include <plugin-sdk/game_sa/engine/OS.h>

#include <algorithm>
#include <vector>

namespace {
struct MediaPlayerClass {
    jclass cls;
    jmethodID init, setDataSource, prepare, start, pause, seekTo, setLooping, setVolume, isPlaying, release;
};

// AML attaches the calling thread to the JVM when needed.
JNIEnv *Env() { return aml ? aml->GetJNIEnvironment() : nullptr; }

// Null if android.media.MediaPlayer can't be resolved.
const MediaPlayerClass *GetClass(JNIEnv *env) {
    static const MediaPlayerClass java = [env] {
        MediaPlayerClass c{};
        jclass local = env->FindClass("android/media/MediaPlayer");
        if (!local) {
            env->ExceptionClear();
            return c;
        }
        c.cls = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        c.init = env->GetMethodID(c.cls, "<init>", "()V");
        c.setDataSource = env->GetMethodID(c.cls, "setDataSource", "(Ljava/lang/String;)V");
        c.prepare = env->GetMethodID(c.cls, "prepare", "()V");
        c.start = env->GetMethodID(c.cls, "start", "()V");
        c.pause = env->GetMethodID(c.cls, "pause", "()V");
        c.seekTo = env->GetMethodID(c.cls, "seekTo", "(I)V");
        c.setLooping = env->GetMethodID(c.cls, "setLooping", "(Z)V");
        c.setVolume = env->GetMethodID(c.cls, "setVolume", "(FF)V");
        c.isPlaying = env->GetMethodID(c.cls, "isPlaying", "()Z");
        c.release = env->GetMethodID(c.cls, "release", "()V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            c.cls = nullptr;
        }
        return c;
    }();
    return java.cls ? &java : nullptr;
}

// Logs and clears a pending Java exception. Returns true if `what` threw.
bool Threw(JNIEnv *env, const char *what) {
    if (!env->ExceptionCheck())
        return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    logger->Error("MusicPlayer: %s failed", what);
    return true;
}

template <typename... Args>
void CallVoid(jobject player, jmethodID MediaPlayerClass::*method, const char *what, Args... args) {
    JNIEnv *env = Env();
    const MediaPlayerClass *java = env ? GetClass(env) : nullptr;
    if (!player || !java)
        return;
    env->CallVoidMethod(player, java->*method, args...);
    Threw(env, what);
}

// Never destroyed, so a static MusicPlayer can still unregister during static destruction.
std::vector<MusicPlayer *> &LoadedPlayers() {
    static auto *players = new std::vector<MusicPlayer *>;
    return *players;
}
std::vector<MusicPlayer *> &PausedInBackground() {
    static auto *players = new std::vector<MusicPlayer *>;
    return *players;
}

void Erase(std::vector<MusicPlayer *> &players, MusicPlayer *player) {
    players.erase(std::remove(players.begin(), players.end(), player), players.end());
}

// MediaPlayer keeps playing in the background on its own, so follow the game's pause/resume.
void OnApplicationEvent(OSEventType type, void *) {
    if (type == OSET_Pause) {
        for (MusicPlayer *player : LoadedPlayers()) {
            if (player->IsPlaying()) {
                player->Pause();
                PausedInBackground().push_back(player);
            }
        }
    } else if (type == OSET_Resume) {
        for (MusicPlayer *player : PausedInBackground())
            player->Play();
        PausedInBackground().clear();
    }
}
} // namespace

bool MusicPlayer::Load(const char *path) {
    Unload();
    JNIEnv *env = Env();
    const MediaPlayerClass *java = env ? GetClass(env) : nullptr;
    if (!java || !path)
        return false;

    jobject player = env->NewObject(java->cls, java->init);
    if (Threw(env, "MediaPlayer()") || !player)
        return false;

    jstring jpath = env->NewStringUTF(path);
    env->CallVoidMethod(player, java->setDataSource, jpath);
    env->DeleteLocalRef(jpath);
    bool ok = !Threw(env, "setDataSource");
    if (ok) {
        env->CallVoidMethod(player, java->prepare); // Synchronous; parsing a local file's header is quick.
        ok = !Threw(env, "prepare");
    }

    if (ok)
        player_ = env->NewGlobalRef(player);
    else
        env->CallVoidMethod(player, java->release);
    env->DeleteLocalRef(player);
    if (!ok) {
        logger->Error("MusicPlayer: can't play %s", path);
        return false;
    }

    static const bool hooked = (Events::applicationEvent.before += OnApplicationEvent, true);
    (void)hooked;
    LoadedPlayers().push_back(this);
    return true;
}

void MusicPlayer::Unload() {
    if (!player_)
        return;
    Erase(LoadedPlayers(), this);
    Erase(PausedInBackground(), this);
    CallVoid(player_, &MediaPlayerClass::release, "release");
    if (JNIEnv *env = Env())
        env->DeleteGlobalRef(player_);
    player_ = nullptr;
}

void MusicPlayer::Play() { CallVoid(player_, &MediaPlayerClass::start, "start"); }

// MediaPlayer.pause() throws unless it is playing.
void MusicPlayer::Pause() {
    if (IsPlaying())
        CallVoid(player_, &MediaPlayerClass::pause, "pause");
}

// MediaPlayer.stop() would require another prepare(), so rewind instead.
void MusicPlayer::Stop() {
    Pause();
    CallVoid(player_, &MediaPlayerClass::seekTo, "seekTo", jint{0});
}

void MusicPlayer::SetLooping(bool looping) {
    CallVoid(player_, &MediaPlayerClass::setLooping, "setLooping", static_cast<jboolean>(looping));
}

void MusicPlayer::SetVolume(float volume) {
    const jfloat gain = std::clamp(volume, 0.0f, 1.0f);
    CallVoid(player_, &MediaPlayerClass::setVolume, "setVolume", gain, gain);
}

bool MusicPlayer::IsPlaying() const {
    JNIEnv *env = Env();
    const MediaPlayerClass *java = env ? GetClass(env) : nullptr;
    if (!player_ || !java)
        return false;
    const bool playing = env->CallBooleanMethod(player_, java->isPlaying);
    return !Threw(env, "isPlaying") && playing;
}
