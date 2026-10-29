// NoAutosave - OBSE64 plugin for The Elder Scrolls IV: Oblivion Remastered.
//
// Stops the engine from autosaving regardless of the in-game toggles.
// See README.md for how the autosave paths were traced.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "PluginAPI.h"
#include "Patcher.h"

namespace {

// ---- version data ----------------------------------------------------------

#define MAKE_EXE_VERSION(major, minor, build) \
	((((major) & 0xF) << 28) | (((minor) & 0xFFF) << 16) | (((build) & 0xFFF) << 4))

constexpr uint32_t kPluginVersion = 0x010001;	// 1.0.1

// ---- logging ---------------------------------------------------------------

FILE *		g_log = nullptr;
std::string	g_dllDir;

void Log(const char * fmt, ...)
{
	if(!g_log) return;
	va_list args;
	va_start(args, fmt);
	vfprintf(g_log, fmt, args);
	va_end(args);
	fputc('\n', g_log);
	fflush(g_log);
}

void InitPaths(bool enableLog)
{
	HMODULE self = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(&InitPaths), &self);

	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	g_dllDir = path;
	const size_t slash = g_dllDir.find_last_of("\\/");
	g_dllDir = (slash == std::string::npos) ? std::string() : g_dllDir.substr(0, slash + 1);

	if(enableLog)
		g_log = fopen((g_dllDir + "NoAutosave.log").c_str(), "w");
}

// ---- patching --------------------------------------------------------------

bool SafeWrite(void * addr, const void * src, size_t len)
{
	DWORD oldProtect = 0;
	if(!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		Log("VirtualProtect failed at %p (error %lu)", addr, GetLastError());
		return false;
	}
	memcpy(addr, src, len);
	VirtualProtect(addr, len, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), addr, len);
	return true;
}

struct TextSection
{
	uint8_t *	base = nullptr;	// image base
	uint32_t	rva = 0;		// RVA of .text
	uint32_t	size = 0;		// virtual size of .text
	uint32_t	imageSize = 0;
};

bool FindTextSection(TextSection & out)
{
	uint8_t * base = reinterpret_cast<uint8_t *>(GetModuleHandleA(nullptr));
	if(!base) return false;

	auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
	if(dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
	auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
	if(nt->Signature != IMAGE_NT_SIGNATURE) return false;

	IMAGE_SECTION_HEADER * sec = IMAGE_FIRST_SECTION(nt);
	for(WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
	{
		if(strncmp(reinterpret_cast<const char *>(sec->Name), ".text", IMAGE_SIZEOF_SHORT_NAME) == 0)
		{
			out.base = base;
			out.rva = sec->VirtualAddress;
			out.size = sec->Misc.VirtualSize;
			out.imageSize = nt->OptionalHeader.SizeOfImage;
			return true;
		}
	}
	return false;
}

bool ApplyPatches(bool disableAll)
{
	TextSection text;
	if(!FindTextSection(text))
	{
		Log("could not locate the game's .text section");
		return false;
	}
	Log("image base %p, .text rva 0x%X size 0x%X", text.base, text.rva, text.size);

	const uint8_t * textBytes = text.base + text.rva;
	noautosave::PatchPlan plan = noautosave::Plan(textBytes, text.size);
	if(!plan.ok)
	{
		Log("signature scan failed: %s", plan.error.c_str());
		Log("this game version is not supported by this build of the plugin; nothing was patched");
		return false;
	}

	// Validate that the function we found really is the autosave request:
	// its lea r8 must point at the string "autosave".
	const uint64_t strAddr = noautosave::AutosaveStringAddress(
		reinterpret_cast<uint64_t>(text.base), text.rva, textBytes, plan.autosaveRequestOffset);
	const uint64_t imageStart = reinterpret_cast<uint64_t>(text.base);
	if(strAddr < imageStart || strAddr + 9 > imageStart + text.imageSize ||
		memcmp(reinterpret_cast<const char *>(strAddr), noautosave::kAutosaveRequest_ExpectedString, 9) != 0)
	{
		Log("validation failed: autosave request function does not reference \"autosave\" (points at 0x%llX)",
			static_cast<unsigned long long>(strAddr));
		Log("nothing was patched");
		return false;
	}

	uint8_t * requestFn = text.base + text.rva + plan.autosaveRequestOffset;
	uint8_t * orInstr = text.base + text.rva + plan.worldspaceTriggerOffset;
	Log("autosave request function at rva 0x%llX", static_cast<unsigned long long>(text.rva + plan.autosaveRequestOffset));
	Log("worldspace-change autosave trigger at rva 0x%llX", static_cast<unsigned long long>(text.rva + plan.worldspaceTriggerOffset));

	bool ok = true;
	if(disableAll)
	{
		// Make the autosave request function return immediately.  Every
		// autosave trigger (door/area transitions, fast travel, rest, wait,
		// level-up, scripted) funnels through it.  Manual saves and quicksaves
		// use different callers and are untouched.
		ok &= SafeWrite(requestFn, noautosave::kPatch_ReturnFalse, sizeof(noautosave::kPatch_ReturnFalse));
		Log("patched: all autosaves disabled");
	}
	else
	{
		// Only remove the trigger that ignores the in-game settings.  The
		// fast travel / rest / wait / interior-exterior toggles keep working.
		ok &= SafeWrite(orInstr, noautosave::kPatch_Nop7, sizeof(noautosave::kPatch_Nop7));
		Log("patched: worldspace-change autosave trigger disabled (in-game toggles still control the rest)");
	}
	return ok;
}

}	// namespace

extern "C" {

__declspec(dllexport) OBSEPluginVersionData OBSEPlugin_Version =
{
	OBSEPluginVersionData::kVersion,

	kPluginVersion,
	"NoAutosave",
	"adam",

	OBSEPluginVersionData::kAddressIndependence_Signatures,	// we find our addresses by signature scanning
	OBSEPluginVersionData::kStructureIndependence_NoStructs,	// we don't touch any game structures
	{
		MAKE_EXE_VERSION(1, 512, 105),	// 1.512.105 (the version this was developed against)
		MAKE_EXE_VERSION(1, 512, 102),	// value OBSE64 0.2.2's public header uses for the same release
		0
	},

	0,	// no minimum OBSE64 version
	0, 0, { 0 }
};

__declspec(dllexport) bool OBSEPlugin_Load(const OBSEInterface * obse)
{
	InitPaths(false);
	const std::string ini = g_dllDir + "NoAutosave.ini";
	const bool enableLog = GetPrivateProfileIntA("NoAutosave", "bEnableLog", 0, ini.c_str()) != 0;
	const bool disableAll = GetPrivateProfileIntA("NoAutosave", "bDisableAllAutosaves", 1, ini.c_str()) != 0;
	InitPaths(enableLog);

	Log("NoAutosave 1.0.1 loading");
	Log("obse64 version 0x%08X, runtime version 0x%08X", obse->obse64Version, obse->runtimeVersion);
	Log("ini: %s (bDisableAllAutosaves=%d, bEnableLog=%d)", ini.c_str(), disableAll ? 1 : 0, enableLog ? 1 : 0);

	ApplyPatches(disableAll);

	// Returning true even on failure keeps OBSE64 from reporting a fatal plugin
	// error; the log explains what happened and the game is left untouched.
	return true;
}

}	// extern "C"
