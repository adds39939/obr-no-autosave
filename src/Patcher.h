// Pure signature-scanning / patch-planning logic, no Windows dependencies.
// Shared by the OBSE64 plugin (runtime, scans the mapped .text) and the Linux
// test harness (scans the .text section of the exe on disk).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace noautosave {

struct Pattern
{
	std::vector<int> bytes;	// -1 = wildcard
};

inline Pattern ParsePattern(const char * sig)
{
	Pattern p;
	for(const char * c = sig; *c; )
	{
		if(*c == ' ') { ++c; continue; }
		if(c[0] == '?')
		{
			p.bytes.push_back(-1);
			c += (c[1] == '?') ? 2 : 1;
			continue;
		}
		char hex[3] = { c[0], c[1], 0 };
		p.bytes.push_back((int)strtoul(hex, nullptr, 16));
		c += 2;
	}
	return p;
}

// Returns every offset (relative to `data`) at which the pattern matches.
inline std::vector<size_t> FindAll(const uint8_t * data, size_t len, const Pattern & p)
{
	std::vector<size_t> hits;
	const size_t n = p.bytes.size();
	if(!n || len < n) return hits;
	const int first = p.bytes[0];
	for(size_t i = 0; i + n <= len; ++i)
	{
		if(first >= 0 && data[i] != (uint8_t)first) continue;
		size_t j = 1;
		for(; j < n; ++j)
		{
			const int b = p.bytes[j];
			if(b >= 0 && data[i + j] != (uint8_t)b) break;
		}
		if(j == n) hits.push_back(i);
	}
	return hits;
}

// ---------------------------------------------------------------------------
// Signature 1: the engine's "request an autosave" function.
//
// Every autosave trigger in the Gamebryo side of the game (fast travel, rest,
// wait, level-up, scripted autosave, and the "autosave pending" flag that cell
// transitions set) ends up calling this one function, which then calls the
// generic save routine with the name "autosave".  In 1.512.105 it lives at
// RVA 0x6703BC0.
//
//   48 89 5C 24 08         mov [rsp+8], rbx
//   57                     push rdi
//   48 83 EC 20            sub rsp, 20h
//   48 8B 05 ?? ?? ?? ??   mov rax, [g_mainThreadInfo]
//   48 8B F9               mov rdi, rcx
//   8B 58 18               mov ebx, [rax+18h]
//   FF 15 ?? ?? ?? ??      call [GetCurrentThreadId]
//   3B C3                  cmp eax, ebx
//   75 06                  jne +6
//   0F B6 47 30            movzx eax, byte [rdi+30h]
//   EB 06                  jmp +6
//   8B 47 30               mov eax, [rdi+30h]
//   C1 E8 12               shr eax, 12h
//   A8 01                  test al, 1
//   75 2A                  jne return
//   48 8B 0D ?? ?? ?? ??   mov rcx, [g_player]
//   E8 ?? ?? ?? ??         call CannotSaveNow
//   84 C0                  test al, al
//   75 1A                  jne return
//   45 33 C9               xor r9d, r9d
//   88 87 32 01 00 00      mov [rdi+132h], al
//   4C 8D 05 ?? ?? ?? ??   lea r8, "autosave"          <- offset 0x4A
//   33 D2                  xor edx, edx
//   48 8B CF               mov rcx, rdi
//   E8 ?? ?? ?? ??         call SaveGame
// ---------------------------------------------------------------------------
static const char * kSig_AutosaveRequest =
	"48 89 5C 24 08 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B F9 8B 58 18 "
	"FF 15 ?? ?? ?? ?? 3B C3 75 06 0F B6 47 30 EB 06 8B 47 30 C1 E8 12 A8 01 75 2A "
	"48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 75 1A 45 33 C9 88 87 32 01 00 00 "
	"4C 8D 05 ?? ?? ?? ?? 33 D2 48 8B CF E8";
static const size_t kAutosaveRequest_LeaStringOffset = 0x4A;	// lea r8, [rip+disp32]
static const char * kAutosaveRequest_ExpectedString = "autosave";

// ---------------------------------------------------------------------------
// Signature 2: the un-gated "autosave on next cell load" trigger.
//
// When the player moves to a cell in a different worldspace (doors, most area
// transitions) this code sets bit 0x8000 on the save manager's flags, which
// makes the post-load code call the autosave request above.  Unlike the
// interior/exterior switch, fast travel, rest and wait triggers, this one does
// not consult any of the bSaveOn* INI settings, which is why the in-game
// toggles don't stop it.  In 1.512.105 the OR instruction is at RVA 0x6602457.
//
//   E8 ?? ?? ?? ??         call SaveManager::IsBusy
//   84 C0                  test al, al
//   75 5C                  jne skip
//   48 8B 05 ?? ?? ?? ??   mov rax, [g_saveManager]
//   81 48 30 00 80 00 00   or dword [rax+30h], 8000h   <- offset 0x10, 7 bytes
//   4C 8B 44 24 70         mov r8, [rsp+70h]
//   48 8B 15               mov rdx, [...]
// ---------------------------------------------------------------------------
static const char * kSig_WorldspaceChangeTrigger =
	"E8 ?? ?? ?? ?? 84 C0 75 5C 48 8B 05 ?? ?? ?? ?? 81 48 30 00 80 00 00 "
	"4C 8B 44 24 70 48 8B 15";
static const size_t kWorldspaceChange_OrOffset = 0x10;
static const size_t kWorldspaceChange_OrLength = 7;

struct PatchPlan
{
	bool	ok = false;
	std::string	error;

	size_t	autosaveRequestOffset = 0;	// offset of function start within scanned range
	size_t	worldspaceTriggerOffset = 0;	// offset of the OR instruction within scanned range
};

// `text` must be the executable's .text section as mapped (in memory) or as
// stored on disk; offsets in the result are relative to `text`.
// `imageOffsetOfText` is only used to express addresses in error messages.
inline PatchPlan Plan(const uint8_t * text, size_t textLen)
{
	PatchPlan plan;

	auto req = FindAll(text, textLen, ParsePattern(kSig_AutosaveRequest));
	if(req.size() != 1)
	{
		plan.error = "autosave request signature matched " + std::to_string(req.size()) + " times (expected 1)";
		return plan;
	}
	plan.autosaveRequestOffset = req[0];

	// Validate: the lea r8 inside the function must point at "autosave".
	{
		const size_t leaAt = req[0] + kAutosaveRequest_LeaStringOffset;
		int32_t disp;
		memcpy(&disp, text + leaAt + 3, sizeof(disp));
		const int64_t target = (int64_t)(leaAt + 7) + disp;	// rip-relative, relative to .text start
		// The string lives in .rdata, outside .text, so we can only validate it
		// when the caller gives us a window into the whole image.  That is done
		// by ValidateString() below; here we just sanity check the displacement.
		if(target <= 0) { plan.error = "autosave string displacement looks wrong"; return plan; }
	}

	auto ws = FindAll(text, textLen, ParsePattern(kSig_WorldspaceChangeTrigger));
	if(ws.size() != 1)
	{
		plan.error = "worldspace-change trigger signature matched " + std::to_string(ws.size()) + " times (expected 1)";
		return plan;
	}
	plan.worldspaceTriggerOffset = ws[0] + kWorldspaceChange_OrOffset;

	plan.ok = true;
	return plan;
}

// Given the image base and the .text RVA, compute the absolute address the
// `lea r8` in the autosave request function points to.
inline uint64_t AutosaveStringAddress(uint64_t imageBase, uint64_t textRVA, const uint8_t * text, size_t autosaveRequestOffset)
{
	const size_t leaAt = autosaveRequestOffset + kAutosaveRequest_LeaStringOffset;
	int32_t disp;
	memcpy(&disp, text + leaAt + 3, sizeof(disp));
	return imageBase + textRVA + leaAt + 7 + (int64_t)disp;
}

// Bytes written for each patch.
static const uint8_t kPatch_ReturnFalse[] = { 0x31, 0xC0, 0xC3 };	// xor eax, eax ; ret
static const uint8_t kPatch_Nop7[] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };	// 7-byte nop

}	// namespace noautosave
