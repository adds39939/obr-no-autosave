// Copied from https://github.com/ianpatt/obse64 (obse64/PluginAPI.h), OBSE64 0.2.2.
#pragma once

#include <cstdint>

typedef std::uint32_t PluginHandle;	// treat this as an opaque type

class BranchTrampoline;

struct PluginInfo
{
	enum
	{
		kInfoVersion = 1
	};

	std::uint32_t	infoVersion;
	const char *	name;
	std::uint32_t	version;
};

enum
{
	kPluginHandle_Invalid = 0xFFFFFFFF
};

enum
{
	kInterface_Invalid = 0,
	kInterface_Messaging,
	kInterface_Trampoline,
	kInterface_Max,
};

struct OBSEInterface
{
	enum
	{
		kInterfaceVersion = 1
	};

	std::uint32_t	obse64Version;
	std::uint32_t	runtimeVersion;
	std::uint32_t	editorVersion;
	std::uint32_t	isEditor;
	std::uint32_t	interfaceVersion;
	void *	(* QueryInterface)(std::uint32_t id);
	PluginHandle		(* GetPluginHandle)(void);
	std::uint32_t		(* GetReleaseIndex)(void);
	const PluginInfo*	(* GetPluginInfo)(const char * name);
	const char*			(* GetSaveFolderName)(void);
};

struct OBSEMessagingInterface
{
	struct Message {
		const char		* sender;
		std::uint32_t	type;
		std::uint32_t	dataLen;
		void			* data;
	};

	typedef void (* EventCallback)(Message* msg);

	enum {
		kInterfaceVersion = 1
	};

	enum {
		kMessage_PostLoad,
		kMessage_PostPostLoad,
		kMessage_DataLoaded,
	};

	std::uint32_t interfaceVersion;
	bool	(* RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
	bool	(* Dispatch)(PluginHandle sender, std::uint32_t messageType, void * data, std::uint32_t dataLen, const char* receiver);
};

struct OBSETrampolineInterface
{
	enum
	{
		kInterfaceVersion = 1
	};

	std::uint32_t interfaceVersion;

	void * (* AllocateFromBranchPool)(PluginHandle plugin, size_t size);
	void * (* AllocateFromLocalPool)(PluginHandle plugin, size_t size);
};

typedef bool (* _OBSEPlugin_Load)(const OBSEInterface * obse);

struct OBSEPluginVersionData
{
	enum
	{
		kVersion = 1,
	};

	enum
	{
		kAddressIndependence_Signatures = 1 << 0,
		kAddressIndependence_AddressLibrary = 1 << 1,
	};

	enum
	{
		kStructureIndependence_NoStructs = 1 << 0,
		kStructureIndependence_InitialLayout = 1 << 1,
	};

	std::uint32_t	dataVersion;		// set to kVersion

	std::uint32_t	pluginVersion;		// version number of your plugin
	char			name[256];			// null-terminated ASCII plugin name
	char			author[256];		// null-terminated ASCII plugin author name

	std::uint32_t	addressIndependence;
	std::uint32_t	structureIndependence;
	std::uint32_t	compatibleVersions[16];	// zero-terminated list of RUNTIME_VERSION_ defines

	std::uint32_t	seVersionRequired;
	std::uint32_t	reservedNonBreaking;
	std::uint32_t	reservedBreaking;
	std::uint8_t	reserved[512];
};
