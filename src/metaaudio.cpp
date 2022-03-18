#include <metahook.h>
#include <IAudio.h>
#include "cvardef.h"
#include <NightfireFileSystem.h>

#include "snd_local.h"
#include "Utilities/AudioCache.hpp"
#include "Loaders/SoundLoader.hpp"
#include "Vox/VoxManager.hpp"
#include "AudioEngine.hpp"
#include "..\pattern_scanner.h"
#include <platformdll.h>

MetaAudio::SteamAudio gSteamAudio;
static std::shared_ptr<MetaAudio::SoundLoader> sound_loader;
static std::unique_ptr<MetaAudio::AudioEngine> audio_engine;

class CAL_Version : public ConsoleFunction
{
public:
    virtual void run(unsigned int numargs, const char** args)
    {
        audio_engine->AL_Version();
    }
    CAL_Version(const char* _name, const char* _description = "", unsigned int _flags = 0)
    {
        name = _name;
        description = _description;
        flags = _flags;
    }
};
CAL_Version AL_Version("al_version");

class CAL_ResetEFX : public ConsoleFunction
{
public:
    virtual void run(unsigned int numargs, const char** args)
    {
        audio_engine->AL_ResetEFX();
    }
    CAL_ResetEFX(const char* _name, const char* _description = "", unsigned int _flags = 0)
    {
        name = _name;
        description = _description;
        flags = _flags;
    }
};
CAL_ResetEFX AL_ResetEFX("al_reset_efx");

class CAL_BasicDevices : public ConsoleFunction
{
public:
    virtual void run(unsigned int numargs, const char** args)
    {
        audio_engine->AL_Devices(true);
    }
    CAL_BasicDevices(const char* _name, const char* _description = "", unsigned int _flags = 0)
    {
        name = _name;
        description = _description;
        flags = _flags;
    }
};
CAL_BasicDevices AL_BasicDevices("al_show_basic_devices");

class CAL_FullDevices : public ConsoleFunction
{
public:
    virtual void run(unsigned int numargs, const char** args)
    {
        audio_engine->AL_Devices(false);
    }
    CAL_FullDevices(const char* _name, const char* _description = "", unsigned int _flags = 0)
    {
        name = _name;
        description = _description;
        flags = _flags;
    }
};
CAL_FullDevices AL_FullDevices("al_show_full_devices");


cl_enginefunc_t* g_pEngfuncs = nullptr;
extern aud_export_t gAudExports;
HINSTANCE g_hInstance, g_hThisModule, g_hSteamAudioInstance;
DWORD g_dwEngineBase, g_dwEngineSize;
DWORD g_dwEngineBuildnum;
NightfirePlatformFuncs* g_pNightfirePlatformFuncs;
NightfireFileSystem* g_pNightfireFileSystem;
bool g_bMetaAudioInitialized = false;

// nightfire methods..
extern "C" __declspec(dllexport) void __cdecl StartMetaAudio(unsigned long hEngineDLL, NightfirePlatformFuncs* platform, NightfireFileSystem* filesystem, cl_exportfuncs_t * pExportFunc, cl_enginefunc_t * pEngineFuncs)
{
    if (filesystem->version != NIGHTFIRE_FILESYSTEM_VERSION)
    {
        pEngineFuncs->Con_Printf("ERROR: MetaAudio received incorrect NightfireFileSystem version %i, expected %i\n", filesystem->version, NIGHTFIRE_FILESYSTEM_VERSION);
        return;
    }
    if (filesystem->size != sizeof(NightfireFileSystem))
    {
        pEngineFuncs->Con_Printf("ERROR: MetaAudio received incorrect NightfireFileSystem size %i, expected %i\n", filesystem->size, sizeof(NightfireFileSystem));
        return;
    }

    g_pNightfirePlatformFuncs = platform;
    g_pNightfireFileSystem = filesystem;
    g_pEngfuncs = pEngineFuncs;
    g_dwEngineBase = hEngineDLL;

    g_hSteamAudioInstance = LoadLibrary("phonon.dll");
    if (g_hSteamAudioInstance)
    {
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplCleanup);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplDestroyEnvironment);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplDestroyScene);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplDestroyStaticMesh);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplGetDirectSoundPath);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplCreateScene);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplCreateStaticMesh);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplCreateEnvironment);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplCreateContext);
        SetSteamAudioFunctionPointer(gSteamAudio, g_hSteamAudioInstance, iplDestroyContext);
    }

    
    auto audio_cache = std::make_shared<MetaAudio::AudioCache>();
    sound_loader = std::make_shared<MetaAudio::SoundLoader>(audio_cache);
    audio_engine = std::make_unique<MetaAudio::AudioEngine>(audio_cache, sound_loader);

    MH_Initialize();
    S_FillAddress();
    S_InstallHook(audio_engine.get(), sound_loader.get());

    g_bMetaAudioInitialized = true;
}
extern "C" __declspec(dllexport) void __cdecl ShutdownMetaAudio()
{
    if (!g_bMetaAudioInitialized)
        return;

    sound_loader.reset();
    audio_engine.reset();
    if (g_hSteamAudioInstance)
    {
        FreeLibrary(g_hSteamAudioInstance);
        g_hSteamAudioInstance = nullptr;
    }
    MH_DisableHook(0);
    MH_Uninitialize();
}