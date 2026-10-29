// Linux test: run the plugin's signature scan against the game executable on
// disk and check it finds exactly the expected locations.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include "../src/Patcher.h"

#pragma pack(push, 1)
struct DosHeader { uint16_t e_magic; uint8_t pad[58]; int32_t e_lfanew; };
struct SectionHeader { char Name[8]; uint32_t VirtualSize; uint32_t VirtualAddress; uint32_t SizeOfRawData; uint32_t PointerToRawData; uint8_t rest[16]; };
#pragma pack(pop)

int main(int argc, char ** argv)
{
	if(argc < 2) { fprintf(stderr, "usage: %s <OblivionRemastered-Win64-Shipping.exe> [expectedRequestRVA expectedTriggerRVA]\n", argv[0]); return 2; }
	std::ifstream f(argv[1], std::ios::binary);
	if(!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
	std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

	auto dos = reinterpret_cast<const DosHeader *>(img.data());
	const uint8_t * nt = img.data() + dos->e_lfanew;
	const uint16_t numSections = *reinterpret_cast<const uint16_t *>(nt + 6);
	const uint16_t optSize = *reinterpret_cast<const uint16_t *>(nt + 20);
	const uint64_t imageBase = *reinterpret_cast<const uint64_t *>(nt + 24 + 24);
	auto sections = reinterpret_cast<const SectionHeader *>(nt + 24 + optSize);

	const SectionHeader * text = nullptr;
	for(int i = 0; i < numSections; ++i) if(strncmp(sections[i].Name, ".text", 8) == 0) text = &sections[i];
	if(!text) { fprintf(stderr, ".text not found\n"); return 2; }

	const uint8_t * textBytes = img.data() + text->PointerToRawData;
	const size_t textLen = text->SizeOfRawData;
	noautosave::PatchPlan plan = noautosave::Plan(textBytes, textLen);
	if(!plan.ok) { printf("FAIL: %s\n", plan.error.c_str()); return 1; }

	const uint64_t reqRVA = text->VirtualAddress + plan.autosaveRequestOffset;
	const uint64_t trigRVA = text->VirtualAddress + plan.worldspaceTriggerOffset;
	printf("autosave request function:        rva 0x%llX (va 0x%llX)\n", (unsigned long long)reqRVA, (unsigned long long)(imageBase + reqRVA));
	printf("worldspace-change trigger (or):   rva 0x%llX (va 0x%llX)\n", (unsigned long long)trigRVA, (unsigned long long)(imageBase + trigRVA));

	// validate the "autosave" string reference
	const uint64_t strVA = noautosave::AutosaveStringAddress(imageBase, text->VirtualAddress, textBytes, plan.autosaveRequestOffset);
	const uint64_t strRVA = strVA - imageBase;
	const char * str = nullptr;
	for(int i = 0; i < numSections; ++i)
	{
		const SectionHeader & s = sections[i];
		if(strRVA >= s.VirtualAddress && strRVA < s.VirtualAddress + s.SizeOfRawData)
			str = reinterpret_cast<const char *>(img.data() + s.PointerToRawData + (strRVA - s.VirtualAddress));
	}
	printf("string referenced by request fn:  va 0x%llX -> \"%s\"\n", (unsigned long long)strVA, str ? str : "(unmapped)");
	if(!str || strcmp(str, "autosave") != 0) { printf("FAIL: string validation\n"); return 1; }

	// show the bytes that would be patched
	printf("bytes at request fn:  %02X %02X %02X ...\n", textBytes[plan.autosaveRequestOffset], textBytes[plan.autosaveRequestOffset+1], textBytes[plan.autosaveRequestOffset+2]);
	printf("bytes at trigger:     ");
	for(size_t i = 0; i < 7; ++i) printf("%02X ", textBytes[plan.worldspaceTriggerOffset + i]);
	printf("\n");
	const uint8_t expectOr[7] = { 0x81, 0x48, 0x30, 0x00, 0x80, 0x00, 0x00 };
	if(memcmp(textBytes + plan.worldspaceTriggerOffset, expectOr, 7) != 0) { printf("FAIL: trigger bytes are not the expected OR instruction\n"); return 1; }

	if(argc >= 4)
	{
		const uint64_t expReq = strtoull(argv[2], nullptr, 16), expTrig = strtoull(argv[3], nullptr, 16);
		if(reqRVA != expReq || trigRVA != expTrig) { printf("FAIL: expected rva 0x%llX / 0x%llX\n", (unsigned long long)expReq, (unsigned long long)expTrig); return 1; }
	}
	printf("PASS\n");
	return 0;
}
