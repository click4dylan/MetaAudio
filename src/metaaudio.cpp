#include <metahook.h>
#include <IAudio.h>
#include "cvardef.h"

#include "snd_local.h"
#include "Utilities/AudioCache.hpp"
#include "Loaders/SoundLoader.hpp"
#include "Vox/VoxManager.hpp"
#include "AudioEngine.hpp"

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
NightfireFileSystem g_pNightfireFileSystem;

// nightfire methods..
extern "C" __declspec(dllexport) void __cdecl StartMetaAudio(unsigned long hEngineDLL, cl_exportfuncs_t * pExportFunc, cl_enginefunc_t * pEngineFuncs)
{
    //COM_InitFilesystem: 64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 81 EC 1C 01 00 00
    //COM_FileSeek: 83 EC 08 56 8B 74 24 10 8B 4E 0C 85 C9 74 27 8B 15 ?? ?? ?? ?? 8B 12 8B 01
    //COM_FileTell: 8B 44 24 0C 50 E8 ?? ?? ?? ?? 8B 4C 24 08 83 C4 04 2B C1 C3
    //COM_ReadFile: 8B 44 24 04 8B 48 0C 83 EC 08 85 C9 74 2D 8B 01 8D 54 24 0C 52
    //COM_CloseFile: 8B 4C 24 10 85 C9 74 07 8B 01 6A 01 FF 10 C3 8B 4C 24 0C 51 E8 ?? ?? ?? ?? 59 C3
    //SYS_FileTell: 8B 44 24 04 8B 0C 85 ?? ?? ?? ?? 89 4C 24 04 E9 ?? ?? ?? ??
    //SYS_FileSeek: 8B 44 24 08 8B 4C 24 04 8B 14 8D ?? ?? ?? ?? 6A 00 50 52 E8 ?? ?? ?? ?? 83 C4 0C C3
    //SYS_FileClose: 56 8B 74 24 08 85 F6 7C 1B
    //SYS_FileWrite: second instance of (+0x30) 8B 44 24 04 8B 0C 85 ?? ?? ?? ?? 8B 54 24 0C 8B 44 24 08 51 52 6A 01 50 E8 ?? ?? ?? ?? 83 C4 10 C3
    //COM_WriteFile(const char*name, void* data, int len): 8B 44 24 04 81 EC 04 01 00 00 50 68 ?? ?? ?? ?? 68 ?? ?? ?? ??
    //COM_WriteFile(HCOMFILE& file, const void* data, int len): 8B 54 24 04 8B 42 08 89 44 24 04 E9
    //COM_FileExists: 8B 4C 24 04 83 EC 10 53 56 6A 00 8D 44 24 0C 50 51 6A 00 6A 00 32 DB
    //COM_OpenFile: 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 81 EC 38 01 00 00
    /////fixme: COM_FindFileSearch: 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 81 EC 38 01 00 00
    //COM_UngetToken: (+1) 90 C6 05 ?? ?? ?? ?? 01 C3
    //COM_ParseFile: 8B 44 24 04 56 50 E8 ?? ?? ?? ?? 8B 4C 24 14 8B 54 24 10 83 C4 04
    //COM_Parse: A0 ?? ?? ?? ?? 53 33 DB 3A C3 8B
    //COM_FreeFile: 8B 44 24 04 85 C0 74 07 50 FF 15 ?? ?? ?? ?? C3 8B 44 24 0C 81 EC 00 04 00 00
    //COM_LoadHeapFile: 8B 44 24 08 53 8B 5C 24 08 50 B8 02 00 00 00 E8 ?? ?? ?? ?? 83 C4 04 5B C3
    //COM_LoadFile: 81 EC 14 01 00 00 55 8B AC 24 1C 01 00 00 85 ED 56 57 8B F0 74 07


    //pEngineFuncs->COM_LoadHeapFile


    g_dwEngineBase = hEngineDLL;

    auto audio_cache = std::make_shared<MetaAudio::AudioCache>();
    sound_loader = std::make_shared<MetaAudio::SoundLoader>(audio_cache);
    audio_engine = std::make_unique<MetaAudio::AudioEngine>(audio_cache, sound_loader);

    S_FillAddress();
    S_InstallHook(audio_engine.get(), sound_loader.get());
}
extern "C" __declspec(dllexport) void __cdecl ShutdownMetaAudio()
{
    sound_loader.reset();
    audio_engine.reset();
    if (g_hSteamAudioInstance)
    {
        FreeLibrary(g_hSteamAudioInstance);
        g_hSteamAudioInstance = nullptr;
    }
}