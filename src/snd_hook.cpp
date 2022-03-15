#include <metahook.h>

#include "plugins.h"
#include "snd_local.h"
#include "AudioEngine.hpp"
#include "Loaders/SoundLoader.hpp"
#include "..\pattern_scanner.h"

aud_engine_t gAudEngine;

#define GetCallAddress(addr) (addr + (*(DWORD *)((addr)+1)) + 5)
#define Sig_NotFound(name) Sys_ErrorEx("Could not find entrypoint for %s\nEngine buildnum: %d\n %p %X", #name, g_dwEngineBuildnum, g_dwEngineBase, g_dwEngineSize);
#define Sig_FuncNotFound(name) if(!gAudEngine.name) Sig_NotFound(name)
#define Sig_AddrNotFound(name) if(!addr) Sig_NotFound(name)

#define Sig_Length(a) (sizeof(a)-1)
#define Search_Pattern(sig) g_pMetaHookAPI->SearchPattern((void *)g_dwEngineBase, g_dwEngineSize, sig, Sig_Length(sig));
#define Search_Pattern_From(func, sig) g_pMetaHookAPI->SearchPattern((void *)gAudEngine.func, g_dwEngineSize - (DWORD)gAudEngine.func + g_dwEngineBase, sig, Sig_Length(sig));
#define InstallHook(func) HookFunctionWithMinHook((LPVOID)gAudEngine.func, func, (LPVOID *)&gAudEngine.func)
//#define InstallHook(func) g_pMetaHookAPI->InlineHook((void *)gAudEngine.func, func, (void *&)gAudEngine.func);



void S_FillAddress()
{
    memset(&gAudEngine, 0, sizeof(gAudEngine));

    //S_EndPrecaching: 8B 0D ?? ?? ?? ?? 85 C9 74 05 E9 ?? ?? ?? ?? C3

    gAudEngine.S_Init = (void(*)(void))FindMemoryPattern(g_dwEngineBase, "6A FF 68 ? ? ? ? 64 A1 ? ? ? ? 50 64 89 25 ? ? ? ? 51 A0", false);
    Sig_FuncNotFound(S_Init);

    gAudEngine.S_Shutdown = (void(*)(void))FindMemoryPattern(g_dwEngineBase, "A0 ? ? ? ? 84 C0 74 26 E8 ? ? ? ? 8B 0D ? ? ? ? 85 C9", false);
    Sig_FuncNotFound(S_Shutdown);

    gAudEngine.S_FindName = (sfx_t * (*)(char*, int*))FindMemoryPattern(g_dwEngineBase, "53 55 56 8B 74 24 10 33 DB 85 F6 57", false);
    Sig_FuncNotFound(S_FindName);

    gAudEngine.S_PrecacheSound = (sfx_t * (*)(char*))FindMemoryPattern(g_dwEngineBase, "8B 0D ? ? ? ? 85 C9 74 0B 8B 44 24 04 50", false);
    Sig_FuncNotFound(S_PrecacheSound);
    gAudEngine.g_gearBoxAudio = (void*)*(DWORD*)((DWORD)gAudEngine.S_PrecacheSound + 2);

    gAudEngine.SND_Spatialize = (void(*)(aud_channel_t*))FindMemoryPattern(g_dwEngineBase, "83 EC 18 56 8B 74 24 20 8B 46 18 57 8B F9 3B 05", false);
    Sig_FuncNotFound(SND_Spatialize);

    gAudEngine.S_StartDynamicSound = (void(*)(int, int, sfx_t*, float*, float, float, int, int))FindMemoryPattern(g_dwEngineBase, "8B 0D ? ? ? ? 85 C9 74 2D 8B 54 24 20 52 8B 54 24 20 8B 01 52 8B 54 24 20 52 8B 54 24 20 52 8B 54 24 20 52 8B 54 24 20 52 8B 54 24 20 52 8B 54 24 20 52 FF 50 0C C3", false);//"83 EC 48 57 8B F9 8A 47 04 84 C0 0F 84 ? ? ? ? 53 55 56 8B 74 24 64 85 F6 0F 84", false);
    Sig_FuncNotFound(S_StartDynamicSound);

    gAudEngine.S_StartStaticSound = (void(*)(int, int, sfx_t*, float*, float, float, int, int))FindMemoryPattern(g_dwEngineBase, "8B 0D ? ? ? ? 85 C9 74 2D 8B 54 24 20 52 8B 54 24 20 8B 01 52 8B 54 24 20 52 8B 54 24 20 52 8B 54 24 20 52 8B 54 24 20 52 8B 54 24 20 52 8B 54 24 20 52 FF 50 08 C3", false);//"83 EC 48 57 8B F9 8A 47 04 84 C0 0F 84 ? ? ? ? 53 8B 5C 24 5C", false);
    Sig_FuncNotFound(S_StartStaticSound);

    gAudEngine.S_StopSound = (void(*)(int, int))FindMemoryPattern(g_dwEngineBase, "8B 0D ? ? ? ? 85 C9 74 0F 8B 54 24 08 8B 01 52 8B 54 24 08 52 FF 50 1C C3", false);
    Sig_FuncNotFound(S_StopSound);

    gAudEngine.S_StopAllSounds = (void(*)(qboolean))FindMemoryPattern(g_dwEngineBase, "8B 0D ? ? ? ? 85 C9 74 0B 0F B6 54 24 04 8B 01 52 FF 50 20 C3", false);
    Sig_FuncNotFound(S_StopAllSounds);

    gAudEngine.S_Update = (void(*)(float*, float*, float*, float*))FindMemoryPattern(g_dwEngineBase, "8B 0D ? ? ? ? 85 C9 74 19 8B 54 24 10 8B 01 52 8B 54 24 10 52 8B 54 24 10 52 8B 54 24 10 52 FF 50 18 C3", false);
    Sig_FuncNotFound(S_Update);

    gAudEngine.S_LoadSound = (aud_sfxcache_t * (__fastcall *)(void* pgbaudio, void* edx, sfx_t*, aud_channel_t*))FindMemoryPattern(g_dwEngineBase, "81 EC 54 03 00 00 53 8B D9 8B 8C 24 5C 03 00 00 33 C0 55 8D 69 01 89 44 24 1C 89 44 24 18", false);
    Sig_FuncNotFound(S_LoadSound);

    //gAudEngine.SequenceGetSentenceByIndex = (sentenceEntry_s * (*)(unsigned int))Search_Pattern(SEQUENCE_GETSENTENCEBYINDEX_SIG);
    //Sig_FuncNotFound(SequenceGetSentenceByIndex);

    //gAudEngine.VoiceSE_Idle = (void(*)(float))Search_Pattern(VOICESE_IDLE_SIG);
    //Sig_FuncNotFound(VoiceSE_Idle);

    DWORD addr;

    //addr = (DWORD)g_pMetaHookAPI->SearchPattern((void*)gAudEngine.SND_Spatialize, 0x10, "\x8B\x0D", Sig_Length("\x8B\x0D"));
    //Sig_AddrNotFound(cl_viewentity);
    addr = FindMemoryPattern(g_dwEngineBase, "8B 0D ? ? ? ? 3B 0D ? ? ? ? 0F 8F ? ? ? ? 83 78 4C 03", false);
    Sig_AddrNotFound(cl_viewentity);
    gAudEngine.cl_viewentity = *(int**)((DWORD)addr + 2);
    gAudEngine.cl_num_entities = gAudEngine.cl_viewentity + 3;
    
    addr = FindMemoryPattern(g_dwEngineBase, "89 35 ? ? ? ? 23 F2 89 35", false);
    Sig_AddrNotFound(cl_parsecount);
    gAudEngine.cl_parsecount = *(int**)(addr + 2);
    gAudEngine.cl_servercount = gAudEngine.cl_parsecount - 2;

    addr = FindMemoryPattern(g_dwEngineBase, "89 0D ? ? ? ? 8B 93 ? 00 00 00 33 F6 3B C6", false);
    Sig_AddrNotFound(cl_waterlevel);
    gAudEngine.cl_waterlevel = *(int**)(addr + 2);

    gAudEngine.cl_time = (double*)(gAudEngine.cl_waterlevel + 10);
    gAudEngine.cl_oldtime = gAudEngine.cl_time + 1;
    gAudEngine.VOX_Shutdown = (void(*)(void))FindMemoryPattern(g_dwEngineBase, "A1 ? ? ? ? 56 57 33 FF 33 F6 3B C7 7E 26 90", false);
    gAudEngine.cszrawsentences = *(int**)((DWORD)gAudEngine.VOX_Shutdown + 1);
    gAudEngine.rgpszrawsentence = *(char* (**)[CVOXFILESENTENCEMAX])((DWORD)gAudEngine.VOX_Shutdown + 0x13);
    gAudEngine.S_FreeChannel = (void(*)(channel_t*))FindMemoryPattern(g_dwEngineBase, "56 57 8B 7C 24 0C 83 7F 1C 05 75 2B");

    //addr = (DWORD)g_pMetaHookAPI->SearchPattern((void*)gAudEngine.S_FreeChannel, 0x50, "\x50\xE8\x2A\x2A\x2A\x2A\x83\xC4\x04", Sig_Length("\x50\xE8\x2A\x2A\x2A\x2A\x83\xC4\x04"));
    //Sig_AddrNotFound(VoiceSE_NotifyFreeChannel);
    //gAudEngine.VoiceSE_NotifyFreeChannel = (void(*)(int))GetCallAddress(addr + 1);

    //addr = (DWORD)g_pMetaHookAPI->SearchPattern((void*)gAudEngine.VoiceSE_Idle, 0x100, "\xD8\x05\x2A\x2A\x2A\x2A\xD9\x1D", Sig_Length("\xD8\x05\x2A\x2A\x2A\x2A\xD9\x1D"));
    //Sig_AddrNotFound(g_SND_VoiceOverdrive);
    //gAudEngine.g_SND_VoiceOverdrive = *(float**)(addr + 8);

//#define S_STARTUP_SIG "\xE8\x2A\x2A\x2A\x2A\xE8\x2A\x2A\x2A\x2A\x68\x2A\x2A\x2A\x2A\x68\x00\x20\x01\x00\xE8"
    //addr = (DWORD)g_pMetaHookAPI->SearchPattern((void*)gAudEngine.S_Init, 0x500, S_STARTUP_SIG, Sig_Length(S_STARTUP_SIG));
    //Sig_AddrNotFound(S_Startup);
    //gAudEngine.S_Startup = (void(*)(void))GetCallAddress(addr);
}

#ifdef _DEBUG
void Sys_Error(char *fmt, ...)
{
  return gAudEngine.Sys_Error(fmt);
}
#endif

static MetaAudio::AudioEngine* p_engine;
static MetaAudio::SoundLoader* p_loader;

//static void S_Startup() { p_engine->S_Startup(); }
static void S_Init() { p_engine->S_Init(); }
static void S_Shutdown() { p_engine->S_Shutdown(); }
static sfx_t* S_FindName(char* name, int* pfInCache) { return p_engine->S_FindName(name, pfInCache); }
static void S_StartDynamicSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch) { p_engine->S_StartDynamicSound(entnum, entchannel, sfx, origin, fvol, attenuation, flags, pitch); };
static void S_StartStaticSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch) { p_engine->S_StartStaticSound(entnum, entchannel, sfx, origin, fvol, attenuation, flags, pitch); };
static void S_StopSound(int entnum, int entchannel) { p_engine->S_StopSound(entnum, entchannel); };
static void S_StopAllSounds(qboolean clear) { p_engine->S_StopAllSounds(clear); }
static void S_Update(float* origin, float* forward, float* right, float* up) { p_engine->S_Update(origin, forward, right, up); }
static aud_sfxcache_t* __fastcall S_LoadSound(void* pgbaudio, void* edx, sfx_t* s, aud_channel_t* ch) { return p_loader->S_LoadSound(s, ch); }

void S_InstallHook(MetaAudio::AudioEngine* engine, MetaAudio::SoundLoader* loader)
{
  p_engine = engine;
  p_loader = loader;
  //InstallHook(S_Startup);
  InstallHook(S_Init);
  InstallHook(S_Shutdown);
  InstallHook(S_FindName);
  InstallHook(S_StartDynamicSound);
  InstallHook(S_StartStaticSound);
  InstallHook(S_StopSound);
  InstallHook(S_StopAllSounds);
  InstallHook(S_Update);
  InstallHook(S_LoadSound);
#ifdef _DEBUG
  InstallHook(Sys_Error);
#endif
}