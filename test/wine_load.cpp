// Windows test: load the DLL the way OBSE64 would, print the version data and
// call OBSEPlugin_Load with a stub interface.  Run under Wine.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "../src/PluginAPI.h"

static void * QI(std::uint32_t) { return nullptr; }
static PluginHandle GH() { return 1; }
static std::uint32_t RI() { return 1; }
static const PluginInfo * PI(const char *) { return nullptr; }
static const char * SF() { return "Oblivion Remastered"; }

int main(int argc, char ** argv)
{
	const char * dll = argc > 1 ? argv[1] : "NoAutosave.dll";
	HMODULE h = LoadLibraryA(dll);
	if(!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
	auto ver = reinterpret_cast<OBSEPluginVersionData *>(GetProcAddress(h, "OBSEPlugin_Version"));
	auto load = reinterpret_cast<_OBSEPlugin_Load>(GetProcAddress(h, "OBSEPlugin_Load"));
	if(!ver || !load) { printf("exports missing (version %p load %p)\n", (void*)ver, (void*)load); return 1; }
	printf("dataVersion=%u pluginVersion=0x%X name=\"%s\" author=\"%s\" addrIndep=%u structIndep=%u compat[0]=0x%08X compat[1]=0x%08X compat[2]=0x%08X\n",
		ver->dataVersion, ver->pluginVersion, ver->name, ver->author, ver->addressIndependence, ver->structureIndependence,
		ver->compatibleVersions[0], ver->compatibleVersions[1], ver->compatibleVersions[2]);
	OBSEInterface iface = { 0x00020200, 0x12000690, 0, 0, OBSEInterface::kInterfaceVersion, QI, GH, RI, PI, SF };
	bool r = load(&iface);
	printf("OBSEPlugin_Load returned %d\n", r ? 1 : 0);
	return 0;
}
