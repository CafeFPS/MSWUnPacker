/*
 * DXBC Shader Support for MSWUnPacker
 *
 * Implementation of CB layout detection and CB2<->CB3 swap patching.
 * Ported from shader_patcher project for S9->S3 conversion.
 */

#include "dxbc.h"
#include <array>
#include <algorithm>

#ifdef _WIN32
#include <intrin.h>  // For _rotl, _rotr
#else
// Fallback for non-MSVC compilers
inline uint32_t _rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t _rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
#endif

namespace dxbc {

// ============================================================================
// DXBC Hash Algorithm
// Custom MD5-like hash used by DirectX Bytecode containers.
// Based on the implementation from HLSLDecompiler/Assembler.cpp
// ============================================================================

static std::array<uint32_t, 4> ComputeHash(const uint8_t* input, uint32_t size) {
    uint32_t esi;
    uint32_t ebx;
    uint32_t i = 0;
    uint32_t edi;
    uint32_t edx;
    uint32_t processedSize = 0;

    uint32_t sizeHash = size & 0x3F;
    bool sizeHash56 = sizeHash >= 56;
    uint32_t restSize = sizeHash56 ? 120 - 56 : 56 - sizeHash;
    uint32_t loopSize = (size + 8 + restSize) >> 6;

    uint32_t Dst[16];
    uint32_t Data[] = { 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t loopSize2 = loopSize - (sizeHash56 ? 2 : 1);

    const uint32_t* pSrc = reinterpret_cast<const uint32_t*>(input);
    uint32_t h[] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476 };

    if (loopSize > 0) {
        while (i < loopSize) {
            if (i == loopSize2) {
                if (!sizeHash56) {
                    // Final block with length at start
                    Dst[0] = size << 3;
                    uint32_t remSize = size - processedSize;
                    std::memcpy(&Dst[1], pSrc, remSize);
                    std::memcpy(&Dst[1 + remSize / 4], Data, restSize);
                    Dst[15] = (size * 2) | 1;
                    pSrc = Dst;
                } else {
                    // Intermediate block (sizeHash56 = true)
                    uint32_t remSize = size - processedSize;
                    std::memcpy(&Dst[0], pSrc, remSize);
                    std::memcpy(&Dst[remSize / 4], Data, 64 - remSize);
                    pSrc = Dst;
                }
            } else if (i > loopSize2) {
                // Length-only block
                Dst[0] = size << 3;
                std::memcpy(&Dst[1], &Data[1], 56);
                Dst[15] = (size * 2) | 1;
                pSrc = Dst;
            }

            // Initial values from memory
            edx = h[0];
            ebx = h[1];
            edi = h[2];
            esi = h[3];

            // Round 1
            edx = _rotl((~ebx & esi | ebx & edi) + pSrc[0] + 0xD76AA478 + edx, 7) + ebx;
            esi = _rotl((~edx & edi | edx & ebx) + pSrc[1] + 0xE8C7B756 + esi, 12) + edx;
            edi = _rotr((~esi & ebx | esi & edx) + pSrc[2] + 0x242070DB + edi, 15) + esi;
            ebx = _rotr((~edi & edx | edi & esi) + pSrc[3] + 0xC1BDCEEE + ebx, 10) + edi;
            edx = _rotl((~ebx & esi | ebx & edi) + pSrc[4] + 0xF57C0FAF + edx, 7) + ebx;
            esi = _rotl((~edx & edi | ebx & edx) + pSrc[5] + 0x4787C62A + esi, 12) + edx;
            edi = _rotr((~esi & ebx | esi & edx) + pSrc[6] + 0xA8304613 + edi, 15) + esi;
            ebx = _rotr((~edi & edx | edi & esi) + pSrc[7] + 0xFD469501 + ebx, 10) + edi;
            edx = _rotl((~ebx & esi | ebx & edi) + pSrc[8] + 0x698098D8 + edx, 7) + ebx;
            esi = _rotl((~edx & edi | ebx & edx) + pSrc[9] + 0x8B44F7AF + esi, 12) + edx;
            edi = _rotr((~esi & ebx | esi & edx) + pSrc[10] + 0xFFFF5BB1 + edi, 15) + esi;
            ebx = _rotr((~edi & edx | edi & esi) + pSrc[11] + 0x895CD7BE + ebx, 10) + edi;
            edx = _rotl((~ebx & esi | ebx & edi) + pSrc[12] + 0x6B901122 + edx, 7) + ebx;
            esi = _rotl((~edx & edi | ebx & edx) + pSrc[13] + 0xFD987193 + esi, 12) + edx;
            edi = _rotr((~esi & ebx | esi & edx) + pSrc[14] + 0xA679438E + edi, 15) + esi;
            ebx = _rotr((~edi & edx | edi & esi) + pSrc[15] + 0x49B40821 + ebx, 10) + edi;

            // Round 2
            edx = _rotl((~esi & edi | esi & ebx) + pSrc[1] + 0xF61E2562 + edx, 5) + ebx;
            esi = _rotl((~edi & ebx | edi & edx) + pSrc[6] + 0xC040B340 + esi, 9) + edx;
            edi = _rotl((~ebx & edx | ebx & esi) + pSrc[11] + 0x265E5A51 + edi, 14) + esi;
            ebx = _rotr((~edx & esi | edx & edi) + pSrc[0] + 0xE9B6C7AA + ebx, 12) + edi;
            edx = _rotl((~esi & edi | esi & ebx) + pSrc[5] + 0xD62F105D + edx, 5) + ebx;
            esi = _rotl((~edi & ebx | edi & edx) + pSrc[10] + 0x02441453 + esi, 9) + edx;
            edi = _rotl((~ebx & edx | ebx & esi) + pSrc[15] + 0xD8A1E681 + edi, 14) + esi;
            ebx = _rotr((~edx & esi | edx & edi) + pSrc[4] + 0xE7D3FBC8 + ebx, 12) + edi;
            edx = _rotl((~esi & edi | esi & ebx) + pSrc[9] + 0x21E1CDE6 + edx, 5) + ebx;
            esi = _rotl((~edi & ebx | edi & edx) + pSrc[14] + 0xC33707D6 + esi, 9) + edx;
            edi = _rotl((~ebx & edx | ebx & esi) + pSrc[3] + 0xF4D50D87 + edi, 14) + esi;
            ebx = _rotr((~edx & esi | edx & edi) + pSrc[8] + 0x455A14ED + ebx, 12) + edi;
            edx = _rotl((~esi & edi | esi & ebx) + pSrc[13] + 0xA9E3E905 + edx, 5) + ebx;
            esi = _rotl((~edi & ebx | edi & edx) + pSrc[2] + 0xFCEFA3F8 + esi, 9) + edx;
            edi = _rotl((~ebx & edx | ebx & esi) + pSrc[7] + 0x676F02D9 + edi, 14) + esi;
            ebx = _rotr((~edx & esi | edx & edi) + pSrc[12] + 0x8D2A4C8A + ebx, 12) + edi;

            // Round 3
            edx = _rotl((esi ^ edi ^ ebx) + pSrc[5] + 0xFFFA3942 + edx, 4) + ebx;
            esi = _rotl((edi ^ ebx ^ edx) + pSrc[8] + 0x8771F681 + esi, 11) + edx;
            edi = _rotl((ebx ^ edx ^ esi) + pSrc[11] + 0x6D9D6122 + edi, 16) + esi;
            ebx = _rotr((edx ^ esi ^ edi) + pSrc[14] + 0xFDE5380C + ebx, 9) + edi;
            edx = _rotl((esi ^ edi ^ ebx) + pSrc[1] + 0xA4BEEA44 + edx, 4) + ebx;
            esi = _rotl((edi ^ ebx ^ edx) + pSrc[4] + 0x4BDECFA9 + esi, 11) + edx;
            edi = _rotl((ebx ^ edx ^ esi) + pSrc[7] + 0xF6BB4B60 + edi, 16) + esi;
            ebx = _rotr((edx ^ esi ^ edi) + pSrc[10] + 0xBEBFBC70 + ebx, 9) + edi;
            edx = _rotl((esi ^ edi ^ ebx) + pSrc[13] + 0x289B7EC6 + edx, 4) + ebx;
            esi = _rotl((edi ^ ebx ^ edx) + pSrc[0] + 0xEAA127FA + esi, 11) + edx;
            edi = _rotl((ebx ^ edx ^ esi) + pSrc[3] + 0xD4EF3085 + edi, 16) + esi;
            ebx = _rotr((edx ^ esi ^ edi) + pSrc[6] + 0x04881D05 + ebx, 9) + edi;
            edx = _rotl((esi ^ edi ^ ebx) + pSrc[9] + 0xD9D4D039 + edx, 4) + ebx;
            esi = _rotl((edi ^ ebx ^ edx) + pSrc[12] + 0xE6DB99E5 + esi, 11) + edx;
            edi = _rotl((ebx ^ edx ^ esi) + pSrc[15] + 0x1FA27CF8 + edi, 16) + esi;
            ebx = _rotr((edx ^ esi ^ edi) + pSrc[2] + 0xC4AC5665 + ebx, 9) + edi;

            // Round 4
            edx = _rotl(((~esi | ebx) ^ edi) + pSrc[0] + 0xF4292244 + edx, 6) + ebx;
            esi = _rotl(((~edi | edx) ^ ebx) + pSrc[7] + 0x432AFF97 + esi, 10) + edx;
            edi = _rotl(((~ebx | esi) ^ edx) + pSrc[14] + 0xAB9423A7 + edi, 15) + esi;
            ebx = _rotr(((~edx | edi) ^ esi) + pSrc[5] + 0xFC93A039 + ebx, 11) + edi;
            edx = _rotl(((~esi | ebx) ^ edi) + pSrc[12] + 0x655B59C3 + edx, 6) + ebx;
            esi = _rotl(((~edi | edx) ^ ebx) + pSrc[3] + 0x8F0CCC92 + esi, 10) + edx;
            edi = _rotl(((~ebx | esi) ^ edx) + pSrc[10] + 0xFFEFF47D + edi, 15) + esi;
            ebx = _rotr(((~edx | edi) ^ esi) + pSrc[1] + 0x85845DD1 + ebx, 11) + edi;
            edx = _rotl(((~esi | ebx) ^ edi) + pSrc[8] + 0x6FA87E4F + edx, 6) + ebx;
            esi = _rotl(((~edi | edx) ^ ebx) + pSrc[15] + 0xFE2CE6E0 + esi, 10) + edx;
            edi = _rotl(((~ebx | esi) ^ edx) + pSrc[6] + 0xA3014314 + edi, 15) + esi;
            ebx = _rotr(((~edx | edi) ^ esi) + pSrc[13] + 0x4E0811A1 + ebx, 11) + edi;
            edx = _rotl(((~esi | ebx) ^ edi) + pSrc[4] + 0xF7537E82 + edx, 6) + ebx;
            h[0] += edx;
            esi = _rotl(((~edi | edx) ^ ebx) + pSrc[11] + 0xBD3AF235 + esi, 10) + edx;
            h[3] += esi;
            edi = _rotl(((~ebx | esi) ^ edx) + pSrc[2] + 0x2AD7D2BB + edi, 15) + esi;
            h[2] += edi;
            ebx = _rotr(((~edx | edi) ^ esi) + pSrc[9] + 0xEB86D391 + ebx, 11) + edi;
            h[1] += ebx;

            processedSize += 0x40;
            pSrc += 16;
            i++;
        }
    }

    return { h[0], h[1], h[2], h[3] };
}

void UpdateHash(std::vector<uint8_t>& data) {
    if (data.size() < 20) {
        return;  // Invalid DXBC
    }

    // Compute hash on data after the hash field (offset 20+)
    auto hash = ComputeHash(data.data() + 20,
                            static_cast<uint32_t>(data.size() - 20));

    // Write hash to bytes 4-19
    std::memcpy(data.data() + 4, hash.data(), 16);
}

bool VerifyHash(const std::vector<uint8_t>& data) {
    if (data.size() < 20) {
        return false;
    }

    // Compute expected hash
    auto computed = ComputeHash(data.data() + 20,
                                static_cast<uint32_t>(data.size() - 20));

    // Compare with stored hash
    return std::memcmp(data.data() + 4, computed.data(), 16) == 0;
}

// ============================================================================
// Helper: Read null-terminated string from RDEF chunk
// ============================================================================

static std::string ReadRDEFString(const uint8_t* rdefData, size_t rdefSize, uint32_t offset) {
    if (offset >= rdefSize) {
        return "";
    }

    const char* str = reinterpret_cast<const char*>(rdefData + offset);
    size_t maxLen = rdefSize - offset;

    // Find null terminator
    size_t len = 0;
    while (len < maxLen && str[len] != '\0') {
        len++;
    }

    return std::string(str, len);
}

// ============================================================================
// CB Layout Detection
// Parse RDEF chunk to find CBufCommonPerCamera and CBufModelInstance slots
// ============================================================================

CBLayoutInfo DetectCBLayout(const uint8_t* data, size_t size) {
    CBLayoutInfo info = { -1, -1, false, "" };

    // Find RDEF chunk
    size_t rdefOffset = FindChunk(data, size, "RDEF");
    if (rdefOffset == 0) {
        info.reason = "No RDEF chunk found";
        return info;
    }

    uint32_t rdefSize = GetChunkSize(data, size, "RDEF");
    if (rdefSize < sizeof(RDEFHeader)) {
        info.reason = "RDEF chunk too small";
        return info;
    }

    const uint8_t* rdefData = data + rdefOffset;
    const RDEFHeader* header = reinterpret_cast<const RDEFHeader*>(rdefData);

    uint32_t bindingCount = header->bindingCount;
    uint32_t bindingOffset = header->bindingOffset;

    const uint32_t BIND_DESC_SIZE = 32;  // sizeof(ShaderInputBindDesc)

    if (bindingOffset + bindingCount * BIND_DESC_SIZE > rdefSize) {
        info.reason = "Invalid RDEF binding table";
        return info;
    }

    // Scan through bindings to find CB slots
    for (uint32_t i = 0; i < bindingCount; i++) {
        uint32_t bindDescOffset = bindingOffset + i * BIND_DESC_SIZE;
        const ShaderInputBindDesc* bindDesc = reinterpret_cast<const ShaderInputBindDesc*>(
            rdefData + bindDescOffset);

        // Only look at cbuffer bindings
        if (bindDesc->type != SIT_CBUFFER) {
            continue;
        }

        // Read the name
        std::string name = ReadRDEFString(rdefData, rdefSize, bindDesc->nameOffset);

        // Check for CBufCommonPerCamera
        if (name == "CBufCommonPerCamera") {
            info.cameraSlot = static_cast<int>(bindDesc->bindPoint);
        }
        // Check for CBufModelInstance
        else if (name == "CBufModelInstance") {
            info.modelInstanceSlot = static_cast<int>(bindDesc->bindPoint);
        }
    }

    // Determine if swap is needed
    // S9 layout: Camera=CB3, ModelInstance=CB2
    // S7/S3 layout: Camera=CB2, ModelInstance=CB3
    if (info.cameraSlot == 3) {
        info.needsSwap = true;
        if (info.modelInstanceSlot == 2) {
            info.reason = "S9 layout detected (Camera=CB3, ModelInstance=CB2)";
        } else if (info.modelInstanceSlot == -1) {
            info.reason = "S9 layout detected (Camera=CB3, no ModelInstance)";
        } else {
            info.reason = "S9 layout detected (Camera=CB3)";
        }
    } else if (info.cameraSlot == 2) {
        info.needsSwap = false;
        info.reason = "S7/S3 layout detected (Camera=CB2)";
    } else if (info.cameraSlot == -1) {
        info.needsSwap = false;
        info.reason = "CBufCommonPerCamera not found";
    } else {
        info.needsSwap = false;
        info.reason = "Unknown layout (Camera=CB" + std::to_string(info.cameraSlot) + ")";
    }

    return info;
}

// ============================================================================
// CB2<->CB3 Swap Patching
// Swap all CB2 and CB3 references in both SHEX and RDEF chunks
// ============================================================================

PatchResult SwapCB2CB3(std::vector<uint8_t>& data) {
    PatchResult result = { true, 0, 0, 0, "" };

    if (data.size() < sizeof(DXBCHeader)) {
        result.success = false;
        result.error = "Data too small to be valid DXBC";
        return result;
    }

    // Verify DXBC magic
    if (memcmp(data.data(), "DXBC", 4) != 0) {
        result.success = false;
        result.error = "Invalid DXBC magic";
        return result;
    }

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data.data());
    uint32_t chunkCount = header->chunkCount;

    if (data.size() < sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t)) {
        result.success = false;
        result.error = "Data too small for chunk offsets";
        return result;
    }

    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data.data() + sizeof(DXBCHeader));

    // Process each chunk
    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > data.size()) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data.data() + offset);
        size_t chunkDataOffset = offset + sizeof(ChunkHeader);
        uint32_t chunkSize = chunk->size;

        if (chunkDataOffset + chunkSize > data.size()) continue;

        // Process SHEX or SHDR chunks
        if (memcmp(chunk->fourCC, "SHEX", 4) == 0 || memcmp(chunk->fourCC, "SHDR", 4) == 0) {
            result.shexPatches += PatchSHEXSwap(data.data() + chunkDataOffset, chunkSize);
        }
        // Process RDEF chunk
        else if (memcmp(chunk->fourCC, "RDEF", 4) == 0) {
            result.rdefPatches += PatchRDEFSwap(data.data() + chunkDataOffset, chunkSize);
        }
    }

    // Update hash after patching
    if (result.shexPatches > 0 || result.rdefPatches > 0) {
        UpdateHash(data);
    }

    return result;
}

// ============================================================================
// SHEX Chunk CB2<->CB3 Swap
// ============================================================================

static uint32_t PatchSHEXSwap(uint8_t* shexData, size_t shexSize) {
    if (shexSize < 8) {
        return 0;
    }

    uint32_t* dwords = reinterpret_cast<uint32_t*>(shexData);
    size_t dwordCount = shexSize / 4;

    // Collect CB2 and CB3 positions for swap
    std::vector<size_t> cb2Positions;
    std::vector<size_t> cb3Positions;

    // Skip version and length tokens
    size_t pos = 2;

    while (pos < dwordCount) {
        uint32_t opcodeToken = dwords[pos];
        uint32_t opcode = GetOpcodeFromToken(opcodeToken);
        uint32_t instLength = GetInstructionLength(opcodeToken);

        if (instLength == 0) {
            instLength = 1;  // Minimum instruction length
        }

        // Check for dcl_constantbuffer (opcode 0x59)
        if (opcode == OPCODE_DCL_CONSTANT_BUFFER) {
            if (pos + 2 < dwordCount) {
                uint32_t cbIndex = dwords[pos + 2];
                size_t indexPos = pos + 2;

                if (cbIndex == 2) {
                    cb2Positions.push_back(indexPos);
                } else if (cbIndex == 3) {
                    cb3Positions.push_back(indexPos);
                }
            }
        }

        // Scan for CB operand references in all instructions
        for (size_t j = pos + 1; j < pos + instLength && j + 1 < dwordCount; j++) {
            uint32_t token = dwords[j];

            if (IsCBOperand(token)) {
                size_t indexPos = j + 1;
                if (IsExtendedOperand(token)) {
                    indexPos++;
                }

                if (indexPos < dwordCount && indexPos < pos + instLength) {
                    uint32_t cbIndex = dwords[indexPos];

                    if (cbIndex == 2) {
                        cb2Positions.push_back(indexPos);
                    } else if (cbIndex == 3) {
                        cb3Positions.push_back(indexPos);
                    }
                }
            }
        }

        pos += instLength;
    }

    // Apply the swap: CB2->CB3, CB3->CB2
    for (size_t idx : cb2Positions) {
        dwords[idx] = 3;
    }
    for (size_t idx : cb3Positions) {
        dwords[idx] = 2;
    }

    return static_cast<uint32_t>(cb2Positions.size() + cb3Positions.size());
}

// ============================================================================
// RDEF Chunk CB2<->CB3 Swap
// ============================================================================

static uint32_t PatchRDEFSwap(uint8_t* rdefData, size_t rdefSize) {
    if (rdefSize < sizeof(RDEFHeader)) {
        return 0;
    }

    const RDEFHeader* header = reinterpret_cast<const RDEFHeader*>(rdefData);
    uint32_t bindingCount = header->bindingCount;
    uint32_t bindingOffset = header->bindingOffset;

    const uint32_t BIND_DESC_SIZE = 32;  // sizeof(ShaderInputBindDesc)

    if (bindingOffset + bindingCount * BIND_DESC_SIZE > rdefSize) {
        return 0;  // Invalid RDEF
    }

    // Collect CB2 and CB3 bindings for swap
    std::vector<ShaderInputBindDesc*> cb2Bindings;
    std::vector<ShaderInputBindDesc*> cb3Bindings;

    for (uint32_t i = 0; i < bindingCount; i++) {
        uint32_t bindDescOffset = bindingOffset + i * BIND_DESC_SIZE;
        ShaderInputBindDesc* bindDesc = reinterpret_cast<ShaderInputBindDesc*>(
            rdefData + bindDescOffset);

        // Only swap cbuffer bindings
        if (bindDesc->type != SIT_CBUFFER) {
            continue;
        }

        if (bindDesc->bindPoint == 2) {
            cb2Bindings.push_back(bindDesc);
        } else if (bindDesc->bindPoint == 3) {
            cb3Bindings.push_back(bindDesc);
        }
    }

    // Apply the swap: 2->3, 3->2
    for (auto* desc : cb2Bindings) {
        desc->bindPoint = 3;
    }
    for (auto* desc : cb3Bindings) {
        desc->bindPoint = 2;
    }

    return static_cast<uint32_t>(cb2Bindings.size() + cb3Bindings.size());
}

// ============================================================================
// SRV Slot Remapping
// Check if an SRV slot should be remapped
// ============================================================================

// Check if an SRV slot should be remapped based on slot number only
// Used for SHEX patching where we don't have resource names
bool ShouldRemapSRV(uint32_t slot, uint32_t& newSlot, bool srvLegacyMode,
                    const std::vector<SRVRemap>& customRemaps) {
    // Check legacy mode first (default S9->S3 remappings)
    if (srvLegacyMode) {
        // Known S9->S3 SRV slot remappings:
        // g_modelInst: t75 -> t61 (unique slot, safe to remap)
        if (slot == 75) {
            newSlot = 61;
            return true;
        }
        // Note: t63 is shared by multiple resources, so we only remap it
        // when we can verify the resource name (in RDEF)
    }

    // Check explicit custom remappings
    for (const auto& remap : customRemaps) {
        if (remap.sourceSlot == slot) {
            newSlot = remap.targetSlot;
            return true;
        }
    }

    return false;
}

// Check if an SRV should be remapped based on resource name (more precise)
// Used for RDEF patching where we have access to resource names
bool ShouldRemapSRVByName(const std::string& name, uint32_t slot, uint32_t& newSlot, bool srvLegacyMode) {
    if (!srvLegacyMode) {
        return false;
    }

    // Known S9->S3 SRV slot remappings by resource name:

    // g_modelInst: t75 -> t61
    if (name == "g_modelInst" && slot == 75) {
        newSlot = 61;
        return true;
    }

    // g_boneWeightsExtra: t63 -> t1 (only this resource at t63 should be remapped)
    if (name == "g_boneWeightsExtra" && slot == 63) {
        newSlot = 1;
        return true;
    }

    return false;
}

// ============================================================================
// Patch SRV references in RDEF chunk
// ============================================================================

static uint32_t PatchSRVInRDEF(uint8_t* rdefData, size_t rdefSize, bool srvLegacyMode,
                               const std::vector<SRVRemap>& customRemaps,
                               std::vector<std::pair<uint32_t, uint32_t>>* remappedSlots = nullptr) {
    if (rdefSize < sizeof(RDEFHeader)) {
        return 0;
    }

    uint32_t patchCount = 0;

    const RDEFHeader* header = reinterpret_cast<const RDEFHeader*>(rdefData);
    uint32_t bindingCount = header->bindingCount;
    uint32_t bindingOffset = header->bindingOffset;

    const uint32_t BIND_DESC_SIZE = 32;

    if (bindingOffset + bindingCount * BIND_DESC_SIZE > rdefSize) {
        return 0;
    }

    // Scan through bindings for SRV types (structured buffers, textures, etc.)
    for (uint32_t i = 0; i < bindingCount; i++) {
        uint32_t bindDescOffset = bindingOffset + i * BIND_DESC_SIZE;
        ShaderInputBindDesc* bindDesc = reinterpret_cast<ShaderInputBindDesc*>(
            rdefData + bindDescOffset);

        // Check for SRV types: STRUCTURED (5), TEXTURE (2), TBUFFER (1), BYTEADDRESS (7)
        if (bindDesc->type != SIT_STRUCTURED &&
            bindDesc->type != SIT_TEXTURE &&
            bindDesc->type != SIT_TBUFFER &&
            bindDesc->type != SIT_BYTEADDRESS) {
            continue;
        }

        // Read the resource name
        std::string resourceName = ReadRDEFString(rdefData, rdefSize, bindDesc->nameOffset);
        uint32_t originalSlot = bindDesc->bindPoint;
        uint32_t newSlot;

        // First try name-based remapping (more precise)
        bool shouldRemap = ShouldRemapSRVByName(resourceName, originalSlot, newSlot, srvLegacyMode);

        // Fall back to slot-based remapping for custom remaps
        if (!shouldRemap) {
            shouldRemap = ShouldRemapSRV(originalSlot, newSlot, srvLegacyMode, customRemaps);
        }

        if (shouldRemap) {
            bindDesc->bindPoint = newSlot;
            patchCount++;

            // Track which slots were remapped so SHEX can use the same mapping
            if (remappedSlots) {
                remappedSlots->push_back({originalSlot, newSlot});
            }
        }
    }

    return patchCount;
}

// ============================================================================
// Patch SRV references in SHEX bytecode
// ============================================================================

// Check if a slot should be remapped based on explicit slot mappings (from RDEF analysis)
static bool ShouldRemapSlot(uint32_t slot, uint32_t& newSlot,
                            const std::vector<std::pair<uint32_t, uint32_t>>& slotMappings,
                            bool srvLegacyMode,
                            const std::vector<SRVRemap>& customRemaps) {
    // First check explicit mappings from RDEF (most accurate)
    for (const auto& mapping : slotMappings) {
        if (mapping.first == slot) {
            newSlot = mapping.second;
            return true;
        }
    }

    // Fall back to generic slot-based remapping
    return ShouldRemapSRV(slot, newSlot, srvLegacyMode, customRemaps);
}

static uint32_t PatchSRVInSHEX(uint8_t* shexData, size_t shexSize, bool srvLegacyMode,
                               const std::vector<SRVRemap>& customRemaps,
                               const std::vector<std::pair<uint32_t, uint32_t>>& slotMappings = {}) {
    if (shexSize < 8) {
        return 0;
    }

    uint32_t* dwords = reinterpret_cast<uint32_t*>(shexData);
    size_t dwordCount = shexSize / 4;

    // Skip version and length tokens
    size_t pos = 2;
    uint32_t patchCount = 0;

    while (pos < dwordCount) {
        uint32_t opcodeToken = dwords[pos];
        uint32_t opcode = GetOpcodeFromToken(opcodeToken);
        uint32_t instLength = GetInstructionLength(opcodeToken);

        if (instLength == 0) {
            instLength = 1;
        }

        // Check for dcl_resource_structured (0xA2), dcl_resource (0x58), dcl_resource_raw (0xA1)
        if (opcode == OPCODE_DCL_RESOURCE_STRUCTURED ||
            opcode == OPCODE_DCL_RESOURCE ||
            opcode == OPCODE_DCL_RESOURCE_RAW) {
            // Resource register index is in the operand following the opcode token
            if (pos + 2 < dwordCount) {
                uint32_t operandToken = dwords[pos + 1];
                size_t indexPos = pos + 2;

                // Check if extended operand
                if (IsExtendedOperand(operandToken)) {
                    indexPos++;
                }

                if (indexPos < dwordCount && indexPos < pos + instLength) {
                    uint32_t srvSlot = dwords[indexPos];
                    uint32_t newSlot;

                    if (ShouldRemapSlot(srvSlot, newSlot, slotMappings, srvLegacyMode, customRemaps)) {
                        dwords[indexPos] = newSlot;
                        patchCount++;
                    }
                }
            }
        }

        // Scan for resource operand references (ld_structured, etc.)
        for (size_t j = pos + 1; j < pos + instLength && j + 1 < dwordCount; j++) {
            uint32_t token = dwords[j];

            if (IsResourceOperand(token)) {
                size_t indexPos = j + 1;
                if (IsExtendedOperand(token)) {
                    indexPos++;
                }

                if (indexPos < dwordCount && indexPos < pos + instLength) {
                    uint32_t srvSlot = dwords[indexPos];
                    uint32_t newSlot;

                    if (ShouldRemapSlot(srvSlot, newSlot, slotMappings, srvLegacyMode, customRemaps)) {
                        dwords[indexPos] = newSlot;
                        patchCount++;
                    }
                }
            }
        }

        pos += instLength;
    }

    return patchCount;
}

// ============================================================================
// Main SRV Patching Function
// ============================================================================

PatchResult PatchSRVSlots(std::vector<uint8_t>& data, bool srvLegacyMode,
                          const std::vector<SRVRemap>& customRemaps) {
    PatchResult result = { true, 0, 0, 0, "" };

    if (data.size() < sizeof(DXBCHeader)) {
        result.success = false;
        result.error = "Data too small to be valid DXBC";
        return result;
    }

    // Verify DXBC magic
    if (memcmp(data.data(), "DXBC", 4) != 0) {
        result.success = false;
        result.error = "Invalid DXBC magic";
        return result;
    }

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data.data());
    uint32_t chunkCount = header->chunkCount;

    if (data.size() < sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t)) {
        result.success = false;
        result.error = "Data too small for chunk offsets";
        return result;
    }

    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data.data() + sizeof(DXBCHeader));

    // Track slot mappings determined from RDEF (using resource names for accuracy)
    std::vector<std::pair<uint32_t, uint32_t>> slotMappings;

    // First pass: Process RDEF chunk to determine slot mappings based on resource names
    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > data.size()) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data.data() + offset);
        size_t chunkDataOffset = offset + sizeof(ChunkHeader);
        uint32_t chunkSize = chunk->size;

        if (chunkDataOffset + chunkSize > data.size()) continue;

        // Process RDEF chunk first to get slot mappings
        if (memcmp(chunk->fourCC, "RDEF", 4) == 0) {
            result.srvPatches += PatchSRVInRDEF(data.data() + chunkDataOffset, chunkSize,
                                                 srvLegacyMode, customRemaps, &slotMappings);
        }
    }

    // Second pass: Process SHEX/SHDR chunks using the slot mappings from RDEF
    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > data.size()) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data.data() + offset);
        size_t chunkDataOffset = offset + sizeof(ChunkHeader);
        uint32_t chunkSize = chunk->size;

        if (chunkDataOffset + chunkSize > data.size()) continue;

        // Process SHEX or SHDR chunks using slot mappings
        if (memcmp(chunk->fourCC, "SHEX", 4) == 0 || memcmp(chunk->fourCC, "SHDR", 4) == 0) {
            result.srvPatches += PatchSRVInSHEX(data.data() + chunkDataOffset, chunkSize,
                                                 srvLegacyMode, customRemaps, slotMappings);
        }
    }

    // Update hash after patching
    if (result.srvPatches > 0) {
        UpdateHash(data);
    }

    return result;
}

// ============================================================================
// Subsurface Material ID Patch
// S9 shaders have extra code for subsurface scattering that causes darker
// rendering. This patch neutralizes that code by replacing the ishr+itof
// instructions that extract the subsurface material ID with mov r6.w, 0 + NOPs.
// ============================================================================

// Pattern: ishr r6.w, cb3[11].w, 16 (extracts subsurface material ID from model instance)
// Note: After CB2<->CB3 swap, the original cb2 becomes cb3
static const uint8_t ISHR_PATTERN[] = {
    0x2a, 0x00, 0x00, 0x08,  // ishr opcode, length 8
    0x82, 0x00, 0x10, 0x00,  // dest r6.w
    0x06, 0x00, 0x00, 0x00,  // dest index
    0x3a, 0x80, 0x20, 0x00,  // cb operand
    0x03, 0x00, 0x00, 0x00,  // cb slot 3 (CBufModelInstance after swap)
    0x0b, 0x00, 0x00, 0x00   // cb register 11
};

// Replacement: mov r6.w, l(0) + 3 NOPs (32 bytes total)
static const uint8_t ISHR_REPLACEMENT[] = {
    0x36, 0x00, 0x00, 0x05,  // mov opcode, length 5
    0x82, 0x00, 0x10, 0x00,  // dest r6.w
    0x06, 0x00, 0x00, 0x00,  // dest index
    0x01, 0x40, 0x00, 0x00,  // immediate operand
    0x00, 0x00, 0x00, 0x00,  // value 0
    0x3a, 0x00, 0x00, 0x01,  // NOP
    0x3a, 0x00, 0x00, 0x01,  // NOP
    0x3a, 0x00, 0x00, 0x01   // NOP
};

// Pattern: itof r6.w, r6.w (converts extracted material ID to float)
static const uint8_t ITOF_PATTERN[] = {
    0x2b, 0x00, 0x00, 0x05,  // itof opcode, length 5
    0x82, 0x00, 0x10, 0x00,  // dest r6.w
    0x06, 0x00, 0x00, 0x00,  // dest index
    0x3a, 0x00, 0x10, 0x00,  // src r6
    0x06, 0x00, 0x00, 0x00   // src index
};

// Replacement: 5 NOPs (20 bytes total)
static const uint8_t ITOF_REPLACEMENT[] = {
    0x3a, 0x00, 0x00, 0x01,  // NOP
    0x3a, 0x00, 0x00, 0x01,  // NOP
    0x3a, 0x00, 0x00, 0x01,  // NOP
    0x3a, 0x00, 0x00, 0x01,  // NOP
    0x3a, 0x00, 0x00, 0x01   // NOP
};

// Pattern 2: and r6.x, cb3[11].w, 0xFFFF0000 (alternative extraction method)
// This is a second extraction pattern found in S9 shaders
static const uint8_t AND_CB3_11_PATTERN[] = {
    0x01, 0x00, 0x00, 0x08,  // and opcode, length 8
    0x42, 0x00, 0x10, 0x00,  // dest r6.x
    0x06, 0x00, 0x00, 0x00,  // dest index (r6)
    0x3a, 0x80, 0x20, 0x00,  // cb operand
    0x03, 0x00, 0x00, 0x00,  // cb slot 3 (CBufModelInstance after swap)
    0x0b, 0x00, 0x00, 0x00,  // cb register 11
    0x01, 0x40, 0x00, 0x00,  // immediate operand
    0xff, 0xff, 0x00, 0x00   // mask value
};

// Replacement: mov r6.x, l(0) + 3 NOPs (32 bytes total)
static const uint8_t AND_CB3_11_REPLACEMENT[] = {
    0x36, 0x00, 0x00, 0x05,  // mov opcode, length 5
    0x42, 0x00, 0x10, 0x00,  // dest r6.x
    0x06, 0x00, 0x00, 0x00,  // dest index (r6)
    0x01, 0x40, 0x00, 0x00,  // immediate operand
    0x00, 0x00, 0x00, 0x00,  // value 0
    0x3a, 0x00, 0x00, 0x01,  // NOP
    0x3a, 0x00, 0x00, 0x01,  // NOP
    0x3a, 0x00, 0x00, 0x01   // NOP
};

static uint32_t PatchSubsurfaceInSHEX(uint8_t* shexData, size_t shexSize) {
    uint32_t patchCount = 0;

    // Search for ishr pattern (first extraction method: ishr r6.w, cb3[11].w, 16)
    for (size_t i = 0; i + sizeof(ISHR_PATTERN) <= shexSize; i++) {
        if (memcmp(shexData + i, ISHR_PATTERN, sizeof(ISHR_PATTERN)) == 0) {
            // Found ishr pattern - replace with mov r6.w, 0 + NOPs
            memcpy(shexData + i, ISHR_REPLACEMENT, sizeof(ISHR_REPLACEMENT));
            patchCount++;

            // Look for itof pattern shortly after (within 64 bytes)
            for (size_t j = i + sizeof(ISHR_REPLACEMENT);
                 j + sizeof(ITOF_PATTERN) <= shexSize && j < i + 64 + sizeof(ISHR_REPLACEMENT);
                 j++) {
                if (memcmp(shexData + j, ITOF_PATTERN, sizeof(ITOF_PATTERN)) == 0) {
                    // Found itof pattern - replace with NOPs
                    memcpy(shexData + j, ITOF_REPLACEMENT, sizeof(ITOF_REPLACEMENT));
                    patchCount++;
                    break;
                }
            }
            break;  // Only patch the first occurrence of ishr
        }
    }

    // Search for AND cb3[11] pattern (second extraction method: and r6.x, cb3[11].w, mask)
    for (size_t i = 0; i + sizeof(AND_CB3_11_PATTERN) <= shexSize; i++) {
        if (memcmp(shexData + i, AND_CB3_11_PATTERN, sizeof(AND_CB3_11_PATTERN)) == 0) {
            // Found and pattern - replace with mov r6.x, 0 + NOPs
            memcpy(shexData + i, AND_CB3_11_REPLACEMENT, sizeof(AND_CB3_11_REPLACEMENT));
            patchCount++;
            break;  // Only patch the first occurrence of and cb3[11]
        }
    }

    return patchCount;
}

PatchResult PatchSubsurfaceMaterialID(std::vector<uint8_t>& data) {
    PatchResult result = { true, 0, 0, 0, "" };

    if (data.size() < sizeof(DXBCHeader)) {
        result.success = false;
        result.error = "Data too small for DXBC header";
        return result;
    }

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data.data());
    if (memcmp(header->magic, "DXBC", 4) != 0) {
        result.success = false;
        result.error = "Invalid DXBC magic";
        return result;
    }

    uint32_t chunkCount = header->chunkCount;
    if (data.size() < sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t)) {
        result.success = false;
        result.error = "Invalid chunk count";
        return result;
    }

    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data.data() + sizeof(DXBCHeader));

    // Process SHEX/SHDR chunks
    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > data.size()) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data.data() + offset);
        size_t chunkDataOffset = offset + sizeof(ChunkHeader);
        uint32_t chunkSize = chunk->size;

        if (chunkDataOffset + chunkSize > data.size()) continue;

        if (memcmp(chunk->fourCC, "SHEX", 4) == 0 || memcmp(chunk->fourCC, "SHDR", 4) == 0) {
            result.shexPatches += PatchSubsurfaceInSHEX(data.data() + chunkDataOffset, chunkSize);
        }
    }

    // Update hash after patching
    if (result.shexPatches > 0) {
        UpdateHash(data);
    }

    return result;
}

// ============================================================================
// Uber Feature Flags Patch
// Patches "and rX.?, cb0[24].?, l(2)" to "mov rX.?, l(0)" + NOPs
// This forces simple blending instead of overlay blending in S9 shaders.
// ============================================================================

// Helper: Check if operand is cb0[24] (CBufUberStatic c_uberFeatureFlags slot)
// Returns true if the operand at the given position is cb0[24].?
static bool IsCB0Slot24Operand(const uint32_t* dwords, size_t pos, size_t instEnd, size_t dwordCount) {
    if (pos >= instEnd || pos >= dwordCount) return false;

    uint32_t operandToken = dwords[pos];

    // Check operand type = constant buffer (8)
    if (GetOperandType(operandToken) != OPERAND_TYPE_CONSTANT_BUFFER) {
        return false;
    }

    // Get CB index position (skip extended operand if present)
    size_t cbIndexPos = pos + 1;
    if (IsExtendedOperand(operandToken)) {
        cbIndexPos++;
    }

    if (cbIndexPos + 1 >= instEnd || cbIndexPos + 1 >= dwordCount) return false;

    // Check CB index is 0 and slot is 24
    uint32_t cbIndex = dwords[cbIndexPos];
    uint32_t cbSlot = dwords[cbIndexPos + 1];

    return (cbIndex == 0 && cbSlot == 24);
}

// Helper: Check if operand is immediate value 1
static bool IsImmediateValue1(const uint32_t* dwords, size_t pos, size_t instEnd, size_t dwordCount) {
    if (pos >= instEnd || pos >= dwordCount) return false;

    uint32_t operandToken = dwords[pos];

    // Immediate operand type = 4 (32-bit immediate)
    // Check bits 12-15 for operand type
    uint32_t opType = (operandToken >> 12) & 0xF;
    if (opType != 4) {
        return false;
    }

    // Get value position
    size_t valuePos = pos + 1;
    if (valuePos >= instEnd || valuePos >= dwordCount) return false;

    return dwords[valuePos] == 1;
}

// Helper: Check if operand is immediate value 2
static bool IsImmediateValue2(const uint32_t* dwords, size_t pos, size_t instEnd, size_t dwordCount) {
    if (pos >= instEnd || pos >= dwordCount) return false;

    uint32_t operandToken = dwords[pos];

    // Immediate operand type = 4 (32-bit immediate)
    // Check bits 12-15 for operand type
    uint32_t opType = (operandToken >> 12) & 0xF;
    if (opType != 4) {
        return false;
    }

    // Get value position
    size_t valuePos = pos + 1;
    if (valuePos >= instEnd || valuePos >= dwordCount) return false;

    return dwords[valuePos] == 2;
}

// Helper: Get the size of an operand in DWORDs
// DXBC operand encoding reference:
//   Bits 0-1: Num components (0=0, 1=1, 2=4)
//   Bits 2-3: Component selection mode (0=mask, 1=swizzle, 2=select1)
//   Bits 4-7: Component mask/swizzle/select depending on mode
//   Bits 12-19: Operand type
//   Bits 20-21: Index dimension (0=0D, 1=1D, 2=2D, 3=3D)
//   Bit 31: Extended operand flag
static size_t GetOperandSize(uint32_t operandToken) {
    // Base size is 1 for the token itself
    size_t size = 1;

    // Extended operand adds 1
    if (IsExtendedOperand(operandToken)) {
        size++;
    }

    // Operand type is in bits 12-19
    uint32_t opType = (operandToken >> 12) & 0xFF;

    // Index dimension in bits 20-21
    uint32_t indexDim = (operandToken >> 20) & 0x3;

    switch (opType) {
        case 0:  // Temp register (r#) - 1D indexed
        case 1:  // Input register (v#) - 1D indexed
        case 2:  // Output register (o#) - 1D indexed
            size += 1;  // Register index
            break;

        case 4:  // Immediate32
            // For scalar immediates, 1 value
            // For vector immediates with mask, count set bits
            {
                uint32_t numComponents = operandToken & 0x3;
                uint32_t compMode = (operandToken >> 2) & 0x3;

                if (numComponents == 2 && compMode == 0) {
                    // 4-component with mask - count set bits in mask
                    uint32_t mask = (operandToken >> 4) & 0xF;
                    int count = 0;
                    while (mask) {
                        count += mask & 1;
                        mask >>= 1;
                    }
                    size += (count > 0) ? count : 1;
                } else {
                    // Scalar or single component
                    size += 1;
                }
            }
            break;

        case 5:  // Immediate64
            size += 2;  // 64-bit value
            break;

        case 7:  // Resource (t#, SRV)
            size += 1;  // Resource index
            break;

        case 8:  // Constant buffer (cb#[#])
            // 2D indexed: buffer index + element index
            size += 2;
            break;

        case 9:  // Sampler (s#)
            size += 1;  // Sampler index
            break;

        case 10: // Label
            size += 1;
            break;

        case 11: // Input primitive ID
        case 12: // Input coverage mask
        case 13: // Input thread ID
        case 14: // Input thread group ID
        case 15: // Input thread ID in group
            // System values - no additional indices typically
            break;

        default:
            // For unknown types, use index dimension
            if (indexDim > 0) {
                size += indexDim;
            } else {
                size += 1;  // Assume at least 1 index
            }
            break;
    }

    return size;
}

static uint32_t PatchUberFlagsInSHEX(uint8_t* shexData, size_t shexSize) {
    if (shexSize < 8) {
        return 0;
    }

    uint32_t* dwords = reinterpret_cast<uint32_t*>(shexData);
    size_t dwordCount = shexSize / 4;

    uint32_t patchCount = 0;

    // Skip version and length tokens
    size_t pos = 2;

    while (pos < dwordCount) {
        uint32_t opcodeToken = dwords[pos];
        uint32_t opcode = GetOpcodeFromToken(opcodeToken);
        uint32_t instLength = GetInstructionLength(opcodeToken);

        if (instLength == 0) {
            instLength = 1;
        }

        size_t instEnd = pos + instLength;
        if (instEnd > dwordCount) {
            break;
        }

        // Look for AND instruction (opcode 0x01)
        if (opcode == OPCODE_AND && instLength >= 4) {
            // AND instruction format:
            // [opcode_token][dest_operand...][src1_operand...][src2_operand...]

            size_t destPos = pos + 1;
            if (destPos < instEnd) {
                uint32_t destToken = dwords[destPos];
                size_t destSize = GetOperandSize(destToken);

                size_t src1Pos = destPos + destSize;
                if (src1Pos < instEnd) {
                    uint32_t src1Token = dwords[src1Pos];
                    size_t src1Size = GetOperandSize(src1Token);

                    size_t src2Pos = src1Pos + src1Size;
                    if (src2Pos < instEnd) {
                        // Check if this is "and rX.?, cb0[24].?, l(2)"
                        bool isCB0_24 = IsCB0Slot24Operand(dwords, src1Pos, instEnd, dwordCount);
                        bool isImm2 = IsImmediateValue2(dwords, src2Pos, instEnd, dwordCount);

                        if (isCB0_24 && isImm2) {
                            // Found the pattern! Replace with mov rX.?, l(0) + NOPs
                            // Original instruction length is in instLength DWORDs

                            // MOV instruction: mov dest, l(0)
                            // Format: [opcode_token][dest_operand...][src_operand...]
                            // We need at least 5 DWORDs for mov: opcode + dest(2) + imm(2)

                            // Build MOV instruction
                            // MOV opcode = 0x36, length = 5 (typical for mov rX.?, l(0))
                            uint32_t movLength = 5;
                            dwords[pos] = OPCODE_MOV | (movLength << 24) | (opcodeToken & 0x00F00000);  // Preserve saturation flag if any

                            // Destination operand stays the same (at pos+1)
                            // It's already there, no change needed

                            // Source operand: immediate 0
                            // Position after destination
                            size_t immPos = destPos + destSize;

                            // Immediate operand token: type 4, 0D dimension, immediate index
                            // 0x01 0x40 0x00 0x00 in bytes = 0x00004001 as DWORD
                            dwords[immPos] = 0x00004001;
                            dwords[immPos + 1] = 0;  // Value = 0

                            // Fill rest with NOPs (opcode 0x3A, length 1)
                            size_t nopStart = immPos + 2;
                            for (size_t i = nopStart; i < instEnd; i++) {
                                dwords[i] = 0x0100003A;  // NOP instruction
                            }

                            patchCount++;
                            printf("           -> Patched AND cb0[24] @ offset %zu\n", pos * 4);
                        }
                    }
                }
            }
        }

        pos += instLength;
    }

    return patchCount;
}

PatchResult PatchUberFeatureFlags(std::vector<uint8_t>& data) {
    PatchResult result = { true, 0, 0, 0, "" };

    if (data.size() < sizeof(DXBCHeader)) {
        result.success = false;
        result.error = "Data too small for DXBC header";
        return result;
    }

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data.data());
    if (memcmp(header->magic, "DXBC", 4) != 0) {
        result.success = false;
        result.error = "Invalid DXBC magic";
        return result;
    }

    uint32_t chunkCount = header->chunkCount;
    if (data.size() < sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t)) {
        result.success = false;
        result.error = "Invalid chunk count";
        return result;
    }

    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data.data() + sizeof(DXBCHeader));

    // Process SHEX/SHDR chunks
    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > data.size()) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data.data() + offset);
        size_t chunkDataOffset = offset + sizeof(ChunkHeader);
        uint32_t chunkSize = chunk->size;

        if (chunkDataOffset + chunkSize > data.size()) continue;

        if (memcmp(chunk->fourCC, "SHEX", 4) == 0 || memcmp(chunk->fourCC, "SHDR", 4) == 0) {
            result.shexPatches += PatchUberFlagsInSHEX(data.data() + chunkDataOffset, chunkSize);
        }
    }

    // Update hash after patching
    if (result.shexPatches > 0) {
        UpdateHash(data);
    }

    return result;
}

// ============================================================================
// Feature Flag Bit 1 Patch (Cavity/AO Blending Mode Fix)
// ============================================================================

// Patch "and rX.?, cb0[24].?, l(1)" instructions in SHEX bytecode
// This forces the AND result to 0, ensuring standard blending mode is used
static uint32_t PatchFeatureFlagBit1InSHEX(uint8_t* shexData, size_t shexSize) {
    if (shexSize < 8) {
        return 0;
    }

    uint32_t* dwords = reinterpret_cast<uint32_t*>(shexData);
    size_t dwordCount = shexSize / 4;

    uint32_t patchCount = 0;

    // Skip version and length tokens
    size_t pos = 2;

    while (pos < dwordCount) {
        uint32_t opcodeToken = dwords[pos];
        uint32_t opcode = GetOpcodeFromToken(opcodeToken);
        uint32_t instLength = GetInstructionLength(opcodeToken);

        if (instLength == 0) {
            instLength = 1;
        }

        size_t instEnd = pos + instLength;
        if (instEnd > dwordCount) {
            break;
        }

        // Look for AND instruction (opcode 0x01)
        if (opcode == OPCODE_AND && instLength >= 4) {
            // AND instruction format:
            // [opcode_token][dest_operand...][src1_operand...][src2_operand...]

            size_t destPos = pos + 1;
            if (destPos < instEnd) {
                uint32_t destToken = dwords[destPos];
                size_t destSize = GetOperandSize(destToken);

                size_t src1Pos = destPos + destSize;
                if (src1Pos < instEnd) {
                    uint32_t src1Token = dwords[src1Pos];
                    size_t src1Size = GetOperandSize(src1Token);

                    size_t src2Pos = src1Pos + src1Size;
                    if (src2Pos < instEnd) {
                        // Check if this is "and rX.?, cb0[24].?, l(1)"
                        bool isCB0_24 = IsCB0Slot24Operand(dwords, src1Pos, instEnd, dwordCount);
                        bool isImm1 = IsImmediateValue1(dwords, src2Pos, instEnd, dwordCount);

                        if (isCB0_24 && isImm1) {
                            // Found the pattern! Replace with mov rX.?, l(0) + NOPs

                            // Build MOV instruction
                            // MOV opcode = 0x36, length = 5 (typical for mov rX.?, l(0))
                            uint32_t movLength = 5;
                            dwords[pos] = OPCODE_MOV | (movLength << 24) | (opcodeToken & 0x00F00000);

                            // Destination operand stays the same (at pos+1)

                            // Source operand: immediate 0
                            size_t immPos = destPos + destSize;
                            dwords[immPos] = 0x00004001;  // Immediate operand token
                            dwords[immPos + 1] = 0;       // Value = 0

                            // Fill rest with NOPs
                            size_t nopStart = immPos + 2;
                            for (size_t i = nopStart; i < instEnd; i++) {
                                dwords[i] = 0x0100003A;  // NOP instruction
                            }

                            patchCount++;
                            printf("           -> Patched AND cb0[24] & 1 @ offset %zu\n", pos * 4);
                        }
                    }
                }
            }
        }

        pos += instLength;
    }

    return patchCount;
}

PatchResult PatchFeatureFlagBit1(std::vector<uint8_t>& data) {
    PatchResult result = { true, 0, 0, 0, "" };

    if (data.size() < sizeof(DXBCHeader)) {
        result.success = false;
        result.error = "Data too small for DXBC header";
        return result;
    }

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data.data());
    if (memcmp(header->magic, "DXBC", 4) != 0) {
        result.success = false;
        result.error = "Invalid DXBC magic";
        return result;
    }

    uint32_t chunkCount = header->chunkCount;
    if (data.size() < sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t)) {
        result.success = false;
        result.error = "Invalid chunk count";
        return result;
    }

    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data.data() + sizeof(DXBCHeader));

    // Process SHEX/SHDR chunks
    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > data.size()) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data.data() + offset);
        size_t chunkDataOffset = offset + sizeof(ChunkHeader);
        uint32_t chunkSize = chunk->size;

        if (chunkDataOffset + chunkSize > data.size()) continue;

        if (memcmp(chunk->fourCC, "SHEX", 4) == 0 || memcmp(chunk->fourCC, "SHDR", 4) == 0) {
            result.shexPatches += PatchFeatureFlagBit1InSHEX(data.data() + chunkDataOffset, chunkSize);
        }
    }

    // Update hash after patching
    if (result.shexPatches > 0) {
        UpdateHash(data);
    }

    return result;
}

// ============================================================================
// Sun Data Unpacking Patch Implementation
// ============================================================================

// Check if operand is cb2[11].w (S9's packedSunData in CBufModelInstance)
// IMPORTANT: We search for cb2 (not cb3) because this patch runs BEFORE the CB swap!
// In S9 layout: cb2 = CBufModelInstance, cb3 = CBufCommonPerCamera
// After CB swap: cb2 = CBufCommonPerCamera, cb3 = CBufModelInstance (S7 layout)
//
// Pattern: token with type=8 (CB), followed by cb_index=2, cb_slot=11
// Component selection can be in different modes:
//   - Mode 0 (Mask): bits 4-7 are a 4-bit mask (0x8 = .w only)
//   - Mode 1 (Swizzle): bits 4-11 contain swizzle (.wwww = 0xFF)
//   - Mode 2 (Select1): bits 4-5 are component index (3 = .w)
static bool IsCB2Slot11W(const uint32_t* dwords, size_t pos, size_t instEnd, size_t dwordCount) {
    if (pos >= instEnd || pos >= dwordCount) return false;

    uint32_t token = dwords[pos];
    uint32_t opType = (token >> 12) & 0xFF;

    // Must be constant buffer operand (type 8)
    if (opType != OPERAND_TYPE_CONSTANT_BUFFER) return false;

    // Check component selection mode (bits 2-3)
    uint32_t compMode = (token >> 2) & 0x3;
    bool isWComponent = false;

    if (compMode == 0) {
        // Mask mode: bits 4-7 are a 4-bit mask
        // .w only = 0x8 (bit 3 set)
        uint32_t mask = (token >> 4) & 0xF;
        isWComponent = (mask == 0x8);
    } else if (compMode == 1) {
        // Swizzle mode: bits 4-11 contain swizzle (2 bits per component)
        // .wwww swizzle = all components select w (3), so 0xFF (11 11 11 11)
        // .w single read could also be represented as first component = 3
        uint32_t swizzle = (token >> 4) & 0xFF;
        // Accept .wwww (0xFF) or any swizzle where first component is .w (bits 4-5 = 3)
        uint32_t firstComp = swizzle & 0x3;
        isWComponent = (swizzle == 0xFF || firstComp == 3);
    } else if (compMode == 2) {
        // Select1 mode: bits 4-5 are the component index (0=x, 1=y, 2=z, 3=w)
        uint32_t componentSel = (token >> 4) & 0x3;
        isWComponent = (componentSel == 3);
    }
    // compMode == 3 is reserved/unused

    if (!isWComponent) return false;

    // Check extended operand
    size_t idx = pos + 1;
    if (IsExtendedOperand(token)) {
        idx++;
    }

    // Need 2 more DWORDs for cb index and slot
    if (idx + 1 >= instEnd || idx + 1 >= dwordCount) return false;

    uint32_t cbIndex = dwords[idx];
    uint32_t cbSlot = dwords[idx + 1];

    // Search for cb2[11] - S9's CBufModelInstance before CB swap
    return (cbIndex == 2 && cbSlot == 11);
}

// Check if operand is immediate 16 (for ishr)
static bool IsImmediate16(const uint32_t* dwords, size_t pos, size_t instEnd, size_t dwordCount) {
    if (pos >= instEnd || pos >= dwordCount) return false;

    uint32_t token = dwords[pos];
    uint32_t opType = (token >> 12) & 0xFF;

    // Must be immediate operand (type 4)
    if (opType != 4) return false;

    size_t idx = pos + 1;
    if (IsExtendedOperand(token)) {
        idx++;
    }

    if (idx >= instEnd || idx >= dwordCount) return false;

    return (dwords[idx] == 16);
}

// Check if operand is immediate 65535 (0xFFFF) (for and)
static bool IsImmediate65535(const uint32_t* dwords, size_t pos, size_t instEnd, size_t dwordCount) {
    if (pos >= instEnd || pos >= dwordCount) return false;

    uint32_t token = dwords[pos];
    uint32_t opType = (token >> 12) & 0xFF;

    // Must be immediate operand (type 4)
    if (opType != 4) return false;

    size_t idx = pos + 1;
    if (IsExtendedOperand(token)) {
        idx++;
    }

    if (idx >= instEnd || idx >= dwordCount) return false;

    return (dwords[idx] == 0xFFFF);
}

// Check if operand is a temp register with .w component
// Used for instanced shader variants where sun data is loaded from structured buffer
// into a temp register before bit extraction
static bool IsTempRegWComponent(const uint32_t* dwords, size_t pos, size_t instEnd,
                                 size_t dwordCount, uint32_t& regIndex) {
    if (pos >= instEnd || pos >= dwordCount) return false;

    uint32_t token = dwords[pos];
    uint32_t opType = (token >> 12) & 0xF;

    // Must be temp register operand (type 0)
    if (opType != 0) return false;

    // Check component selection mode (bits 2-3)
    uint32_t compMode = (token >> 2) & 0x3;
    bool isWComponent = false;

    if (compMode == 0) {
        // Mask mode: bits 4-7 are a 4-bit mask (.w = 0x8)
        uint32_t mask = (token >> 4) & 0xF;
        isWComponent = (mask == 0x8);
    } else if (compMode == 1) {
        // Swizzle mode: bits 4-11 contain swizzle (.wwww = 0xFF)
        uint32_t swizzle = (token >> 4) & 0xFF;
        uint32_t firstComp = swizzle & 0x3;
        isWComponent = (swizzle == 0xFF || firstComp == 3);
    } else if (compMode == 2) {
        // Select1 mode: bits 4-5 are component index (3 = .w)
        uint32_t componentSel = (token >> 4) & 0x3;
        isWComponent = (componentSel == 3);
    }

    if (!isWComponent) return false;

    // Get register index
    size_t idx = pos + 1;
    if (IsExtendedOperand(token)) {
        idx++;
    }

    if (idx >= instEnd || idx >= dwordCount) return false;

    regIndex = dwords[idx];
    return true;
}

// ============================================================================
// Register-Tracked Sun Data Patch
//
// This patch fixes the S9->S7 sun data format incompatibility:
// - S9 expects cb2[11].w as packed int (two 16-bit values) [BEFORE CB swap]
// - S7/R5SDK provides cb3[11].w as direct float (0.0-1.0) [AFTER CB swap]
//
// IMPORTANT: This patch runs BEFORE the CB2<->CB3 swap!
// So we look for cb2[11].w (S9's CBufModelInstance) which will become
// cb3[11].w after the swap (matching S7's layout).
//
// We track the complete instruction sequence:
//   1. ishr rX.w, cb2[11].w, l(16)  -> extract upper 16 bits (sun visibility)
//   2. itof rX.w, rX.w              -> convert to float
//   3. mul rY.w, rX.w, l(3.05e-05)  -> scale to 0.0-1.0
//
// And replace with:
//   1. mov rX.w, cb2[11].w          -> direct float read (becomes cb3 after swap)
//   2. (NOP)                        -> remove int conversion
//   3. mul rY.w, rX.w, l(1.0)       -> multiply by 1.0 (identity)
// ============================================================================

// Structure to track sun data extraction sequences
struct SunDataSequence {
    size_t extractPos;          // Position of ishr/and instruction
    size_t extractLength;       // Length of extract instruction
    size_t convertPos;          // Position of itof/utof instruction
    size_t convertLength;       // Length of convert instruction
    size_t mulPos;              // Position of mul instruction
    size_t mulLength;           // Length of mul instruction
    size_t mulScalePos;         // Position of scaling factor in mul
    uint32_t destRegIndex;      // Destination register index (e.g., 6 for r6)
    uint32_t destComponent;     // Component mask (e.g., 0x80 for .w)
    uint32_t srcRegIndex;       // Source register index (for temp reg sources)
    bool isUpperBits;           // true for ishr (upper 16), false for and (lower 16)
    bool isTempRegSource;       // true if source is temp register, false if CB
    bool foundExtract;
    bool foundConvert;
    bool foundMul;
};

// Extract register index from temp register operand
static uint32_t GetTempRegIndex(const uint32_t* dwords, size_t pos) {
    // Temp register operand: token + index
    // Index is in the next DWORD
    return dwords[pos + 1];
}

// Extract component from operand token
// DXBC operand encoding:
//   Bits 0-1: Index dimension (0=0D, 1=1D, 2=2D)
//   Bits 2-3: Component selection mode (0=mask, 1=swizzle, 2=select1)
//   Bits 4-7: Component mask/swizzle/select depending on mode
static uint32_t GetComponentFromToken(uint32_t token) {
    uint32_t compMode = (token >> 2) & 0x3;  // Bits 2-3 = component selection mode

    if (compMode == 0) {  // Mask mode (typically for destination)
        uint32_t mask = (token >> 4) & 0xF;
        // Find which single bit is set (for scalar operations)
        if (mask == 1) return 0;  // .x
        if (mask == 2) return 1;  // .y
        if (mask == 4) return 2;  // .z
        if (mask == 8) return 3;  // .w
        return mask;  // Return full mask if multiple components
    } else if (compMode == 2) {  // Select1 mode - single component selected
        return (token >> 4) & 0x3;  // Bits 4-5 are the component index
    } else {
        // Swizzle mode - return first component (bits 4-5)
        return (token >> 4) & 0x3;
    }
}

// Check if operand is temp register with specific index
// For scalar operations, we just need to match the register index
static bool IsTempRegIndex(const uint32_t* dwords, size_t pos, size_t instEnd,
                           uint32_t regIndex) {
    if (pos + 1 >= instEnd) return false;

    uint32_t token = dwords[pos];
    uint32_t opType = (token >> 12) & 0xF;

    // Type 0 = temp register
    if (opType != 0) return false;

    // Check register index
    return (dwords[pos + 1] == regIndex);
}

// Check if operand is immediate with specific scaling factor (~3.05e-05)
// Returns position of the value DWORD if found
static bool IsImmediateSunScale(const uint32_t* dwords, size_t pos, size_t instEnd,
                                 size_t& valuePos) {
    if (pos >= instEnd) return false;

    uint32_t token = dwords[pos];
    uint32_t opType = (token >> 12) & 0xF;

    // Type 4 = immediate
    if (opType != 4) return false;

    size_t idx = pos + 1;
    if (IsExtendedOperand(token)) idx++;

    if (idx >= instEnd) return false;

    // Check for specific sun scale factor: 3.05185094e-05 = 0x38000100 or nearby
    // Actually the exact value is 1/32768 = 0x38000000 or 1/65536 = 0x37800000
    // Let's check for the specific values seen in shaders
    uint32_t value = dwords[idx];

    // 3.05185094e-05 in hex (approximately 1/32768)
    // Allow some tolerance for float representation
    float fvalue;
    memcpy(&fvalue, &value, sizeof(float));

    // Check if value is approximately 1/32768 (3.05e-05)
    if (fvalue > 2.5e-05f && fvalue < 3.5e-05f) {
        valuePos = idx;
        return true;
    }

    return false;
}

static uint32_t PatchSunDataInSHEX(uint8_t* shexData, size_t shexSize) {
    if (shexSize < 8) {
        return 0;
    }

    uint32_t* dwords = reinterpret_cast<uint32_t*>(shexData);
    size_t dwordCount = shexSize / 4;

    // Track up to 4 sun data sequences (typically 2: upper and lower 16 bits)
    SunDataSequence sequences[4] = {};
    int seqCount = 0;

    // ========================================================================
    // Pass 1: Find ishr/and instructions on cb2[11].w or temp register .w
    // For instanced variants, sun data is loaded from structured buffer into temp reg
    // ========================================================================
    size_t pos = 2;  // Skip version and length tokens

    while (pos < dwordCount && seqCount < 4) {
        uint32_t opcodeToken = dwords[pos];
        uint32_t opcode = GetOpcodeFromToken(opcodeToken);
        uint32_t instLength = GetInstructionLength(opcodeToken);

        if (instLength == 0) instLength = 1;
        size_t instEnd = pos + instLength;
        if (instEnd > dwordCount) break;

        // Look for ISHR instruction: ishr rX, src.w, l(16)
        // src can be either cb2[11].w (non-instanced) or temp register .w (instanced)
        // NOTE: We look for cb2 because this runs BEFORE CB swap (cb2 = ModelInstance in S9)
        if (opcode == OPCODE_ISHR && instLength >= 4) {
            size_t destPos = pos + 1;
            uint32_t destToken = dwords[destPos];
            size_t destSize = GetOperandSize(destToken);
            size_t src1Pos = destPos + destSize;

            if (src1Pos < instEnd) {
                size_t src1Size = GetOperandSize(dwords[src1Pos]);
                size_t src2Pos = src1Pos + src1Size;

                if (src2Pos < instEnd && IsImmediate16(dwords, src2Pos, instEnd, dwordCount)) {
                    bool isCBSource = IsCB2Slot11W(dwords, src1Pos, instEnd, dwordCount);
                    uint32_t tempSrcReg = 0;
                    bool isTempSource = !isCBSource && IsTempRegWComponent(dwords, src1Pos, instEnd, dwordCount, tempSrcReg);

                    if (isCBSource || isTempSource) {
                        // Record this sequence
                        sequences[seqCount].extractPos = pos;
                        sequences[seqCount].extractLength = instLength;
                        sequences[seqCount].destRegIndex = GetTempRegIndex(dwords, destPos);
                        sequences[seqCount].destComponent = GetComponentFromToken(destToken);
                        sequences[seqCount].srcRegIndex = tempSrcReg;
                        sequences[seqCount].isUpperBits = true;
                        sequences[seqCount].isTempRegSource = isTempSource;
                        sequences[seqCount].foundExtract = true;
                        seqCount++;
                    }
                }
            }
        }
        // Look for AND instruction: and rX.w, src.w, l(65535)
        // src can be either cb2[11].w (non-instanced) or temp register .w (instanced)
        // NOTE: We look for cb2 because this runs BEFORE CB swap (cb2 = ModelInstance in S9)
        else if (opcode == OPCODE_AND && instLength >= 4) {
            size_t destPos = pos + 1;
            uint32_t destToken = dwords[destPos];
            size_t destSize = GetOperandSize(destToken);
            size_t src1Pos = destPos + destSize;

            if (src1Pos < instEnd) {
                size_t src1Size = GetOperandSize(dwords[src1Pos]);
                size_t src2Pos = src1Pos + src1Size;

                if (src2Pos < instEnd && IsImmediate65535(dwords, src2Pos, instEnd, dwordCount)) {
                    bool isCBSource = IsCB2Slot11W(dwords, src1Pos, instEnd, dwordCount);
                    uint32_t tempSrcReg = 0;
                    bool isTempSource = !isCBSource && IsTempRegWComponent(dwords, src1Pos, instEnd, dwordCount, tempSrcReg);

                    if (isCBSource || isTempSource) {
                        // Record this sequence
                        sequences[seqCount].extractPos = pos;
                        sequences[seqCount].extractLength = instLength;
                        sequences[seqCount].destRegIndex = GetTempRegIndex(dwords, destPos);
                        sequences[seqCount].destComponent = GetComponentFromToken(destToken);
                        sequences[seqCount].srcRegIndex = tempSrcReg;
                        sequences[seqCount].isUpperBits = false;
                        sequences[seqCount].isTempRegSource = isTempSource;
                        sequences[seqCount].foundExtract = true;
                        seqCount++;
                    }
                }
            }
        }

        pos += instLength;
    }

    if (seqCount == 0) {
        return 0;  // No sun data patterns found
    }

    // ========================================================================
    // Pass 2: Find itof/utof instructions that convert the tracked registers
    // ========================================================================
    pos = 2;
    while (pos < dwordCount) {
        uint32_t opcodeToken = dwords[pos];
        uint32_t opcode = GetOpcodeFromToken(opcodeToken);
        uint32_t instLength = GetInstructionLength(opcodeToken);

        if (instLength == 0) instLength = 1;
        size_t instEnd = pos + instLength;
        if (instEnd > dwordCount) break;

        // Look for ITOF or UTOF: itof rX, rX
        if ((opcode == OPCODE_ITOF || opcode == OPCODE_UTOF) && instLength >= 3) {
            size_t destPos = pos + 1;
            uint32_t destToken = dwords[destPos];
            size_t destSize = GetOperandSize(destToken);
            size_t srcPos = destPos + destSize;

            if (srcPos < instEnd) {
                uint32_t destReg = GetTempRegIndex(dwords, destPos);

                // Check if this matches any tracked sequence (by register index only)
                for (int i = 0; i < seqCount; i++) {
                    if (sequences[i].foundExtract &&
                        !sequences[i].foundConvert &&
                        sequences[i].destRegIndex == destReg &&
                        pos > sequences[i].extractPos &&
                        pos < sequences[i].extractPos + 200) {  // Must be within ~200 DWORDs

                        // Verify source is same register
                        if (IsTempRegIndex(dwords, srcPos, instEnd, destReg)) {
                            sequences[i].convertPos = pos;
                            sequences[i].convertLength = instLength;
                            sequences[i].foundConvert = true;
                        }
                    }
                }
            }
        }

        pos += instLength;
    }

    // ========================================================================
    // Pass 3: Find MUL instructions with tracked registers and sun scale
    // ========================================================================
    pos = 2;
    while (pos < dwordCount) {
        uint32_t opcodeToken = dwords[pos];
        uint32_t opcode = GetOpcodeFromToken(opcodeToken);
        uint32_t instLength = GetInstructionLength(opcodeToken);

        if (instLength == 0) instLength = 1;
        size_t instEnd = pos + instLength;
        if (instEnd > dwordCount) break;

        // Look for MUL: mul rY, rX, l(3.05e-05)
        if (opcode == OPCODE_MUL && instLength >= 4) {
            size_t destPos = pos + 1;
            uint32_t destToken = dwords[destPos];
            size_t destSize = GetOperandSize(destToken);
            size_t src1Pos = destPos + destSize;

            if (src1Pos < instEnd) {
                uint32_t src1Token = dwords[src1Pos];
                size_t src1Size = GetOperandSize(src1Token);
                size_t src2Pos = src1Pos + src1Size;

                if (src2Pos < instEnd) {
                    // Check both orderings: mul dest, reg, imm OR mul dest, imm, reg
                    size_t scalePos = 0;
                    uint32_t srcRegIndex = 0;
                    bool foundScale = false;

                    // Try src1=reg, src2=scale
                    uint32_t src1Type = (src1Token >> 12) & 0xF;
                    if (src1Type == 0) {  // temp register
                        srcRegIndex = GetTempRegIndex(dwords, src1Pos);
                        if (IsImmediateSunScale(dwords, src2Pos, instEnd, scalePos)) {
                            foundScale = true;
                        }
                    }

                    // Try src1=scale, src2=reg (operands swapped)
                    if (!foundScale && IsImmediateSunScale(dwords, src1Pos, instEnd, scalePos)) {
                        uint32_t src2Token = dwords[src2Pos];
                        uint32_t src2Type = (src2Token >> 12) & 0xF;
                        if (src2Type == 0) {  // temp register
                            srcRegIndex = GetTempRegIndex(dwords, src2Pos);
                            foundScale = true;
                        }
                    }

                    if (foundScale) {
                        // Check if source register matches any tracked sequence
                        for (int i = 0; i < seqCount; i++) {
                            if (sequences[i].foundExtract &&
                                !sequences[i].foundMul &&
                                sequences[i].destRegIndex == srcRegIndex &&
                                pos > sequences[i].extractPos &&
                                pos < sequences[i].extractPos + 600) {  // Must be within ~600 DWORDs

                                sequences[i].mulPos = pos;
                                sequences[i].mulLength = instLength;
                                sequences[i].mulScalePos = scalePos;
                                sequences[i].foundMul = true;
                            }
                        }
                    }
                }
            }
        }

        pos += instLength;
    }

    // ========================================================================
    // Pass 4: Apply patches for complete sequences
    // ========================================================================
    uint32_t patchCount = 0;

    for (int i = 0; i < seqCount; i++) {
        if (!sequences[i].foundExtract) continue;

        // For temp register sources, we MUST have found the sun scale multiply to validate
        // this is actually sun data extraction (could be unrelated ishr rX, rY.w, l(16) otherwise)
        // For CB sources (cb2[11].w), we know it's the sun data slot, so no validation needed
        if (sequences[i].isTempRegSource && !sequences[i].foundMul) {
            continue;  // Skip - can't validate this is sun data
        }

        const char* extractType = sequences[i].isUpperBits ? "ISHR >> 16" : "AND & 0xFFFF";

        // Patch 1: Convert extract (ishr/and) to MOV
        // Both ISHR and AND paths should read the sun visibility float directly
        // NOTE: cb2 is S9's CBufModelInstance (before CB swap), becomes cb3 after swap
        {
            size_t extractPos = sequences[i].extractPos;
            size_t extractLen = sequences[i].extractLength;
            size_t destPos = extractPos + 1;
            uint32_t destToken = dwords[destPos];
            size_t destSize = GetOperandSize(destToken);
            size_t src0Pos = destPos + destSize;
            size_t src0Size = GetOperandSize(dwords[src0Pos]);

            // Both ISHR and AND paths: convert to MOV keeping src0 unchanged
            // ISHR has: dest, src0 (cb2[11].w or r#.w), src1 (shift amount l(16))
            // AND has:  dest, src0 (cb2[11].w or r#.w), src1 (mask l(0xFFFF))
            // We want MOV dest, src0

            uint32_t movLength = 1 + destSize + src0Size;
            dwords[extractPos] = OPCODE_MOV | (movLength << 24);

            // src0 is already in the right position after dest
            // Just fill the rest (src1 = shift/mask) with NOPs
            size_t fillStart = src0Pos + src0Size;
            for (size_t j = fillStart; j < extractPos + extractLen; j++) {
                dwords[j] = 0x0100003A;  // NOP
            }

            patchCount++;
            if (sequences[i].isTempRegSource) {
                printf("           -> [SUN] Patched %s -> MOV from r%u.w @ offset %zu (r%u.%c) [instanced]\n",
                       extractType, sequences[i].srcRegIndex, extractPos * 4,
                       sequences[i].destRegIndex,
                       "xyzw"[sequences[i].destComponent]);
            } else {
                printf("           -> [SUN] Patched %s -> MOV from cb2[11].w @ offset %zu (r%u.%c)\n",
                       extractType, extractPos * 4,
                       sequences[i].destRegIndex,
                       "xyzw"[sequences[i].destComponent]);
            }
        }

        // Patch 2: NOP out the itof/utof conversion (if found)
        // For both ISHR and AND paths: removes int-to-float conversion
        // The value from cb2[11].w is already a float, no conversion needed
        if (sequences[i].foundConvert) {
            size_t convertPos = sequences[i].convertPos;
            size_t convertLen = sequences[i].convertLength;

            for (size_t j = 0; j < convertLen; j++) {
                dwords[convertPos + j] = 0x0100003A;  // NOP
            }

            patchCount++;
            printf("           -> [SUN] NOPed ITOF/UTOF @ offset %zu\n", convertPos * 4);
        }

        // Patch 3: Handle MUL scale factor
        // Both ISHR and AND paths now read cb2[11].w directly as float (0.0-1.0)
        // The scaling by 3.05e-05 (1/32768) was for integer->float conversion
        // Now we just change the scale to 1.0 to preserve the value
        if (sequences[i].foundMul) {
            // Both paths: change scale factor to 1.0
            size_t scalePos = sequences[i].mulScalePos;
            float oldValue;
            memcpy(&oldValue, &dwords[scalePos], sizeof(float));

            dwords[scalePos] = 0x3F800000;  // 1.0f

            patchCount++;
            printf("           -> [SUN] Patched MUL scale %.8e -> 1.0 @ offset %zu (%s)\n",
                   oldValue, sequences[i].mulPos * 4,
                   sequences[i].isUpperBits ? "sun_vis" : "sun_intensity");
        }
    }

    // ========================================================================
    // Pass 5: No additional patching needed
    // Both ISHR and AND paths now read cb2[11].w as direct float, and
    // the MUL scale has been changed to 1.0 to preserve the value.
    // ========================================================================
#if 0  // DISABLED - shadow_blend is now hardcoded to 1.0
    for (int i = 0; i < seqCount; i++) {
        if (sequences[i].isUpperBits) continue;  // Only process AND paths
        if (!sequences[i].foundExtract) continue;

        uint32_t shadowReg = sequences[i].destRegIndex;

        // Search for MUL instructions that use shadowReg as a source
        // and have the same register as both dest and other source
        size_t searchStart = sequences[i].extractPos + sequences[i].extractLength;
        size_t searchEnd = dwordCount;

        for (size_t pos = searchStart; pos < searchEnd; ) {
            uint32_t opcode = dwords[pos] & 0xFF;
            uint32_t instLen = (dwords[pos] >> 24) & 0x7F;
            if (instLen == 0) instLen = 1;

            if (opcode == OPCODE_MUL && instLen >= 4) {
                // MUL has: opcode, dest, src0, src1
                size_t destPos = pos + 1;
                uint32_t destToken = dwords[destPos];
                size_t destSize = GetOperandSize(destToken);

                uint32_t destType = (destToken >> 12) & 0xF;
                uint32_t destIndex = 0;

                // For temp registers with 1D index
                if (destType == 0 && ((destToken >> 20) & 0x3) == 1) {
                    destIndex = dwords[destPos + 1];
                }

                size_t src0Pos = destPos + destSize;
                uint32_t src0Token = dwords[src0Pos];
                size_t src0Size = GetOperandSize(src0Token);

                uint32_t src0Type = (src0Token >> 12) & 0xF;
                uint32_t src0Index = 0;
                size_t src0IndexPos = 0;
                if (src0Type == 0 && ((src0Token >> 20) & 0x3) == 1) {
                    src0IndexPos = src0Pos + 1;
                    src0Index = dwords[src0IndexPos];
                }

                size_t src1Pos = src0Pos + src0Size;
                uint32_t src1Token = dwords[src1Pos];
                size_t src1Size = GetOperandSize(src1Token);

                uint32_t src1Type = (src1Token >> 12) & 0xF;
                uint32_t src1Index = 0;
                size_t src1IndexPos = 0;
                if (src1Type == 0 && ((src1Token >> 20) & 0x3) == 1) {
                    src1IndexPos = src1Pos + 1;
                    src1Index = dwords[src1IndexPos];
                }

                // Check if this is: MUL rX.w, r{shadow}.w, rX.w
                // or: MUL rX.w, rX.w, r{shadow}.w

                // Pattern 1: src0 is shadow register, dest and src1 are same
                if (src0Type == 0 && src0Index == shadowReg &&
                    destType == 0 && src1Type == 0 && destIndex == src1Index) {
                    // Change src0 register from shadowReg to destIndex
                    dwords[src0IndexPos] = destIndex;
                    patchCount++;
                    printf("           -> [SUN] Fixed shadow multiply: r%u -> r%u @ offset %zu\n",
                           shadowReg, destIndex, pos * 4);
                    break;
                }
                // Pattern 2: src1 is shadow register, dest and src0 are same
                if (src1Type == 0 && src1Index == shadowReg &&
                    destType == 0 && src0Type == 0 && destIndex == src0Index) {
                    // Change src1 register from shadowReg to destIndex
                    dwords[src1IndexPos] = destIndex;
                    patchCount++;
                    printf("           -> [SUN] Fixed shadow multiply: r%u -> r%u @ offset %zu\n",
                           shadowReg, destIndex, pos * 4);
                    break;
                }
            }

            pos += instLen;
        }
    }
#endif  // DISABLED Pass 5

    return patchCount;
}

PatchResult PatchSunDataUnpacking(std::vector<uint8_t>& data) {
    PatchResult result = { true, 0, 0, 0, "" };

    if (data.size() < sizeof(DXBCHeader)) {
        result.success = false;
        result.error = "Data too small for DXBC header";
        return result;
    }

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data.data());
    if (memcmp(header->magic, "DXBC", 4) != 0) {
        result.success = false;
        result.error = "Invalid DXBC magic";
        return result;
    }

    uint32_t chunkCount = header->chunkCount;
    if (data.size() < sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t)) {
        result.success = false;
        result.error = "Invalid chunk count";
        return result;
    }

    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data.data() + sizeof(DXBCHeader));

    // Process SHEX/SHDR chunks
    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > data.size()) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data.data() + offset);
        size_t chunkDataOffset = offset + sizeof(ChunkHeader);
        uint32_t chunkSize = chunk->size;

        if (chunkDataOffset + chunkSize > data.size()) continue;

        if (memcmp(chunk->fourCC, "SHEX", 4) == 0 || memcmp(chunk->fourCC, "SHDR", 4) == 0) {
            result.shexPatches += PatchSunDataInSHEX(data.data() + chunkDataOffset, chunkSize);
        }
    }

    // Update hash after patching
    if (result.shexPatches > 0) {
        UpdateHash(data);
    }

    return result;
}

// ============================================================================
// Shadow Blend Multiply NOP Patch
//
// S9 shaders have an extra multiply: r0.w = r0.w * r6.z
// This multiplies sun visibility by shadow blend, but S7 doesn't have this.
// With our sun data patch, both r0.w and r6.z contain the same sun visibility,
// so this becomes sunVis^2 which amplifies frame-to-frame variations causing flickering.
//
// This patch NOPs out the shadow blend multiply to match S7 behavior.
//
// Pattern: mul rX.w, rX.w, rY.z
// Where rX is typically r0 (sun visibility) and rY is typically r6 (shadow blend)
// ============================================================================

// Extract the effective component from an operand token for scalar operations
// For swizzle mode, checks that all components select the same source (scalar read)
// Returns the component index (0=x, 1=y, 2=z, 3=w) or -1 if not a scalar operation
static int GetScalarComponent(uint32_t token) {
    uint32_t compMode = (token >> 2) & 0x3;

    if (compMode == 2) {
        // Select1 mode - bits 4-5 directly give the component
        return (token >> 4) & 0x3;
    }
    else if (compMode == 1) {
        // Swizzle mode - bits 4-11 contain swizzle (2 bits per component: x,y,z,w)
        // For scalar read, all components should select the same source
        uint32_t swizzle = (token >> 4) & 0xFF;
        int comp0 = swizzle & 0x3;         // x component source
        int comp1 = (swizzle >> 2) & 0x3;  // y component source
        int comp2 = (swizzle >> 4) & 0x3;  // z component source
        int comp3 = (swizzle >> 6) & 0x3;  // w component source

        // For scalar operations with .w, swizzle is typically .wwww (0xFF)
        // For scalar operations with .z, swizzle is typically .zzzz (0xAA)
        // Just return the first component, most common case
        return comp0;
    }
    else if (compMode == 0) {
        // Mask mode (typically for destination) - bits 4-7 are mask
        uint32_t mask = (token >> 4) & 0xF;
        if (mask == 1) return 0;  // .x
        if (mask == 2) return 1;  // .y
        if (mask == 4) return 2;  // .z
        if (mask == 8) return 3;  // .w
        return -1;  // Not a single component
    }

    return -1;
}

// Check if this is a "mul r0.w, r4.w, r0.w" pattern (shadow blend multiply)
// S9 shader extracts shadow_blend to r4.w (from AND cb3[11].w & 0xFFFF)
// Then multiplies: mul r0.w, r4.w, r0.w (shadow_blend * shadow_result)
//
// Pattern to detect:
// - Destination is temp register rX with .w mask
// - Source 1 is DIFFERENT temp register rY with .w select (shadow_blend)
// - Source 2 is SAME register rX with .w select (shadow_result)
//
// Note: This multiply does NOT exist in S7 shaders, so we NOP it for S7 compat
static bool IsShadowBlendMultiply(const uint32_t* dwords, size_t pos, size_t instEnd, size_t dwordCount, bool debug = false) {
    size_t destPos = pos + 1;
    if (destPos >= instEnd) return false;

    uint32_t destToken = dwords[destPos];
    uint32_t destType = (destToken >> 12) & 0xF;

    // Destination must be temp register (type 0)
    if (destType != 0) return false;

    // Destination must have .w mask (bit 4-7, mask 0x8 = .w)
    uint32_t destMask = (destToken >> 4) & 0xF;
    if (destMask != 0x8) return false;  // Must be exactly .w

    size_t destSize = GetOperandSize(destToken);
    if (destPos + 1 >= instEnd) return false;
    uint32_t destRegIndex = dwords[destPos + 1];  // Register index

    // Check source 1 and source 2
    size_t src1Pos = destPos + destSize;
    if (src1Pos + 1 >= instEnd) return false;

    uint32_t src1Token = dwords[src1Pos];
    uint32_t src1Type = (src1Token >> 12) & 0xF;
    if (src1Type != 0) return false;  // Source 1 must be temp register

    uint32_t src1RegIndex = dwords[src1Pos + 1];
    int src1Comp = GetScalarComponent(src1Token);
    size_t src1Size = GetOperandSize(src1Token);

    size_t src2Pos = src1Pos + src1Size;
    if (src2Pos + 1 >= instEnd) return false;

    uint32_t src2Token = dwords[src2Pos];
    uint32_t src2Type = (src2Token >> 12) & 0xF;
    if (src2Type != 0) return false;  // Source 2 must be temp register

    uint32_t src2RegIndex = dwords[src2Pos + 1];
    int src2Comp = GetScalarComponent(src2Token);

    // Check for S9 shadow blend multiply pattern:
    // Pattern A: mul rX.w, rY.w, rX.w (dest=src2, src1 is .w from DIFFERENT register)
    //   - dest = rX.w, src1 = rY.w (Y != X), src2 = rX.w (same as dest)
    // Pattern B: mul rX.w, rX.w, rY.w (dest=src1, src2 is .w from DIFFERENT register)
    //   - dest = rX.w, src1 = rX.w (same as dest), src2 = rY.w (Y != X)

    // Both components must be .w (component 3)
    if (src1Comp != 3 || src2Comp != 3) {
        if (debug) {
            printf("             [SKIP] Not all .w components: src1=%c, src2=%c\n",
                   "xyzw"[src1Comp >= 0 ? src1Comp : 0],
                   "xyzw"[src2Comp >= 0 ? src2Comp : 0]);
        }
        return false;
    }

    // One source must match dest, other must be different register
    bool patternA = (src2RegIndex == destRegIndex && src1RegIndex != destRegIndex);
    bool patternB = (src1RegIndex == destRegIndex && src2RegIndex != destRegIndex);

    if (debug) {
        printf("             dest=r%u.w, src1=r%u.%c, src2=r%u.%c\n",
               destRegIndex, src1RegIndex, "xyzw"[src1Comp >= 0 ? src1Comp : 0],
               src2RegIndex, "xyzw"[src2Comp >= 0 ? src2Comp : 0]);
        printf("             patternA=%d (dest=src2), patternB=%d (dest=src1)\n", patternA, patternB);
    }

    // Found the pattern: mul rX.w, rY.w, rX.w OR mul rX.w, rX.w, rY.w
    return patternA || patternB;
}

static uint32_t PatchShadowBlendMultiplyInSHEX(uint8_t* shexData, size_t shexSize) {
    if (shexSize < 8) {
        return 0;
    }

    uint32_t* dwords = reinterpret_cast<uint32_t*>(shexData);
    size_t dwordCount = shexSize / 4;

    uint32_t patchCount = 0;

    // Skip version and length tokens
    size_t pos = 2;

    while (pos < dwordCount) {
        uint32_t opcodeToken = dwords[pos];
        uint32_t opcode = GetOpcodeFromToken(opcodeToken);
        uint32_t instLength = GetInstructionLength(opcodeToken);

        if (instLength == 0) instLength = 1;
        size_t instEnd = pos + instLength;
        if (instEnd > dwordCount) break;

        // Look for MUL instruction (opcode 0x38)
        if (opcode == OPCODE_MUL && instLength >= 4) {
            // Debug: Check if this is close to what we're looking for
            size_t destPos = pos + 1;
            uint32_t destToken = dwords[destPos];
            uint32_t destType = (destToken >> 12) & 0xF;
            uint32_t destMask = (destToken >> 4) & 0xF;

            if (IsShadowBlendMultiply(dwords, pos, instEnd, dwordCount, false)) {
                // Get register info for logging
                uint32_t destToken = dwords[pos + 1];
                uint32_t destRegIndex = dwords[pos + 2];
                size_t destSize = GetOperandSize(destToken);

                uint32_t src1Token = dwords[pos + 1 + destSize];
                uint32_t src1RegIndex = dwords[pos + 1 + destSize + 1];
                int src1Comp = GetScalarComponent(src1Token);
                size_t src1Size = GetOperandSize(src1Token);

                uint32_t src2Token = dwords[pos + 1 + destSize + src1Size];
                uint32_t src2RegIndex = dwords[pos + 1 + destSize + src1Size + 1];
                int src2Comp = GetScalarComponent(src2Token);

                // Replace entire instruction with NOPs
                for (size_t j = 0; j < instLength; j++) {
                    dwords[pos + j] = 0x0100003A;  // NOP
                }

                patchCount++;
                printf("           -> [SHADOW_BLEND] NOPed MUL r%u.w, r%u.%c, r%u.%c @ offset %zu\n",
                       destRegIndex, src1RegIndex, "xyzw"[src1Comp >= 0 ? src1Comp : 0],
                       src2RegIndex, "xyzw"[src2Comp >= 0 ? src2Comp : 0], pos * 4);
            }
        }

        pos += instLength;
    }

    return patchCount;
}

PatchResult PatchShadowBlendMultiply(std::vector<uint8_t>& data) {
    PatchResult result = { true, 0, 0, 0, "" };

    if (data.size() < sizeof(DXBCHeader)) {
        result.success = false;
        result.error = "Data too small for DXBC header";
        return result;
    }

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data.data());
    if (memcmp(header->magic, "DXBC", 4) != 0) {
        result.success = false;
        result.error = "Invalid DXBC magic";
        return result;
    }

    uint32_t chunkCount = header->chunkCount;
    if (data.size() < sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t)) {
        result.success = false;
        result.error = "Invalid chunk count";
        return result;
    }

    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data.data() + sizeof(DXBCHeader));

    // Process SHEX/SHDR chunks
    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > data.size()) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data.data() + offset);
        size_t chunkDataOffset = offset + sizeof(ChunkHeader);
        uint32_t chunkSize = chunk->size;

        if (chunkDataOffset + chunkSize > data.size()) continue;

        if (memcmp(chunk->fourCC, "SHEX", 4) == 0 || memcmp(chunk->fourCC, "SHDR", 4) == 0) {
            result.shexPatches += PatchShadowBlendMultiplyInSHEX(data.data() + chunkDataOffset, chunkSize);
        }
    }

    // Update hash after patching
    if (result.shexPatches > 0) {
        UpdateHash(data);
    }

    return result;
}

// ============================================================================
// ClusteredLighting_t Removal Patch
// S11 shaders declare ClusteredLighting_t in CBufCommonPerCamera but never use it
// This patch reduces the declared buffer size from 784 to 752 bytes
// ============================================================================

// CBufDesc structure (24 bytes per entry in RDEF cbuffer table)
#pragma pack(push, 1)
struct CBufDesc {
    uint32_t nameOffset;     // Offset to name string
    uint32_t variableCount;  // Number of variables in cbuffer
    uint32_t variableOffset; // Offset to variable descriptor table
    uint32_t size;           // Size in bytes (what we patch)
    uint32_t flags;          // Flags
    uint32_t type;           // Type (0 = D3D_CT_CBUFFER)
};
#pragma pack(pop)

PatchResult PatchRemoveClusteredLighting(std::vector<uint8_t>& data) {
    PatchResult result = { true, 0, 0, 0, "" };

    // S11 CBufCommonPerCamera size with ClusteredLighting_t
    constexpr uint32_t S11_CAMERA_BUFFER_SIZE = 784;
    // S7/R5SDK CBufCommonPerCamera size without ClusteredLighting_t
    constexpr uint32_t S7_CAMERA_BUFFER_SIZE = 752;

    if (data.size() < sizeof(DXBCHeader)) {
        result.success = false;
        result.error = "Data too small for DXBC header";
        return result;
    }

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data.data());
    if (memcmp(header->magic, "DXBC", 4) != 0) {
        result.success = false;
        result.error = "Invalid DXBC magic";
        return result;
    }

    // Find RDEF chunk
    size_t rdefOffset = FindChunk(data.data(), data.size(), "RDEF");
    if (rdefOffset == 0) {
        // No RDEF chunk - nothing to patch
        return result;
    }

    uint32_t rdefSize = GetChunkSize(data.data(), data.size(), "RDEF");
    if (rdefSize < sizeof(RDEFHeader)) {
        result.success = false;
        result.error = "RDEF chunk too small";
        return result;
    }

    uint8_t* rdefData = data.data() + rdefOffset;
    const RDEFHeader* rdefHeader = reinterpret_cast<const RDEFHeader*>(rdefData);

    uint32_t cbufferCount = rdefHeader->cbufferCount;
    uint32_t cbufferOffset = rdefHeader->cbufferOffset;

    // Each cbuffer descriptor is 24 bytes
    const uint32_t CBUF_DESC_SIZE = sizeof(CBufDesc);

    if (cbufferOffset + cbufferCount * CBUF_DESC_SIZE > rdefSize) {
        result.success = false;
        result.error = "Invalid cbuffer table";
        return result;
    }

    // Scan through cbuffer descriptors to find CBufCommonPerCamera
    for (uint32_t i = 0; i < cbufferCount; i++) {
        uint32_t cbufDescOffset = cbufferOffset + i * CBUF_DESC_SIZE;
        CBufDesc* cbufDesc = reinterpret_cast<CBufDesc*>(rdefData + cbufDescOffset);

        // Read the name
        std::string name = ReadRDEFString(rdefData, rdefSize, cbufDesc->nameOffset);

        if (name == "CBufCommonPerCamera") {
            // Check if it has the S11 size (784 bytes)
            if (cbufDesc->size == S11_CAMERA_BUFFER_SIZE) {
                // Patch the size to remove ClusteredLighting_t (784 -> 752 bytes)
                cbufDesc->size = S7_CAMERA_BUFFER_SIZE;

                // Also reduce the variable count by 1 to remove ClusteredLighting_t from reflection
                // S11 has 42 variables, S7 has 41 - the extra one is ClusteredLighting_t
                if (cbufDesc->variableCount > 0) {
                    cbufDesc->variableCount--;
                }

                result.rdefPatches++;
            }
            // Found the buffer, no need to continue
            break;
        }
    }

    // Update hash after patching
    if (result.rdefPatches > 0) {
        UpdateHash(data);
    }

    return result;
}

} // namespace dxbc
