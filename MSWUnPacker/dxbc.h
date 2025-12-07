#pragma once
/*
 * DXBC Shader Patching Library
 *
 * Handles constant buffer layout conversion and shader compatibility fixes.
 */

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

namespace dxbc {

// ============================================================================
// DXBC Container Structures
// ============================================================================

#pragma pack(push, 1)

struct DXBCHeader {
    uint8_t  magic[4];          // "DXBC"
    uint8_t  hash[16];          // MD5-like hash
    uint32_t version;           // Usually 1
    uint32_t totalSize;
    uint32_t chunkCount;
    // Followed by chunkCount * uint32_t offsets
};

struct ChunkHeader {
    uint8_t  fourCC[4];         // RDEF, ISGN, OSGN, SHEX, STAT, etc.
    uint32_t size;
};

struct RDEFHeader {
    uint32_t cbufferCount;
    uint32_t cbufferOffset;
    uint32_t bindingCount;
    uint32_t bindingOffset;
    uint8_t  minorVersion;
    uint8_t  majorVersion;
    uint16_t shaderType;
    uint32_t flags;
    uint32_t creatorOffset;
};

struct ShaderInputBindDesc {
    uint32_t nameOffset;
    uint32_t type;              // 0 = CBUFFER
    uint32_t returnType;
    uint32_t dimension;
    uint32_t numSamples;
    uint32_t bindPoint;         // CB slot number
    uint32_t bindCount;
    uint32_t flags;
};

#pragma pack(pop)

// ============================================================================
// Constants
// ============================================================================

// D3D_SHADER_INPUT_TYPE values
constexpr uint32_t SIT_CBUFFER = 0;
constexpr uint32_t SIT_TBUFFER = 1;
constexpr uint32_t SIT_TEXTURE = 2;
constexpr uint32_t SIT_SAMPLER = 3;
constexpr uint32_t SIT_UAV_RWTYPED = 4;
constexpr uint32_t SIT_STRUCTURED = 5;
constexpr uint32_t SIT_UAV_RWSTRUCTURED = 6;
constexpr uint32_t SIT_BYTEADDRESS = 7;

// SM5 Opcodes
constexpr uint32_t OPCODE_DCL_CONSTANT_BUFFER = 0x59;
constexpr uint32_t OPCODE_DCL_RESOURCE = 0x58;
constexpr uint32_t OPCODE_DCL_RESOURCE_RAW = 0xA1;
constexpr uint32_t OPCODE_DCL_RESOURCE_STRUCTURED = 0xA2;
constexpr uint32_t OPCODE_AND = 0x01;
constexpr uint32_t OPCODE_ISHR = 0x2A;
constexpr uint32_t OPCODE_USHR = 0x5E;
constexpr uint32_t OPCODE_MOV = 0x36;
constexpr uint32_t OPCODE_MUL = 0x38;
constexpr uint32_t OPCODE_NOP = 0x3A;
constexpr uint32_t OPCODE_ITOF = 0x2B;
constexpr uint32_t OPCODE_UTOF = 0x56;

// Operand types
constexpr uint32_t OPERAND_TYPE_CONSTANT_BUFFER = 8;
constexpr uint32_t OPERAND_TYPE_RESOURCE = 7;

// ============================================================================
// CB Layout Detection
// ============================================================================

struct CBLayoutInfo {
    int cameraSlot;             // CBufCommonPerCamera slot (-1 if not found)
    int modelInstanceSlot;      // CBufModelInstance slot (-1 if not found)
    bool needsSwap;             // True if CB2<->CB3 swap needed
    std::string reason;
};

CBLayoutInfo DetectCBLayout(const uint8_t* data, size_t size);

// ============================================================================
// Patching Functions
// ============================================================================

struct PatchResult {
    bool success;
    int shexPatches;            // Bytecode patches applied
    int rdefPatches;            // RDEF metadata patches applied
    int srvPatches;             // SRV slot patches applied
    std::string error;
};

struct SRVRemap {
    uint32_t sourceSlot;
    uint32_t targetSlot;
};

// Swap CB2<->CB3 references in both SHEX and RDEF
PatchResult SwapCB2CB3(std::vector<uint8_t>& data);

// Remap SRV slots (e.g., t75->t61 for g_modelInst)
PatchResult PatchSRVSlots(std::vector<uint8_t>& data, bool srvLegacyMode = true,
                          const std::vector<SRVRemap>& customRemaps = {});

bool ShouldRemapSRV(uint32_t slot, uint32_t& newSlot, bool srvLegacyMode,
                    const std::vector<SRVRemap>& customRemaps);

bool ShouldRemapSRVByName(const std::string& name, uint32_t slot, uint32_t& newSlot, bool srvLegacyMode);

// Neutralize subsurface material ID extraction (fixes darker rendering)
PatchResult PatchSubsurfaceMaterialID(std::vector<uint8_t>& data);

// Force simple blending mode (fixes detail texture overlay issues)
PatchResult PatchUberFeatureFlags(std::vector<uint8_t>& data);

// Force standard cavity/AO blending (fixes blending mode issues)
PatchResult PatchFeatureFlagBit1(std::vector<uint8_t>& data);

// Fix packed sun data reads (converts bit unpacking to direct float read)
PatchResult PatchSunDataUnpacking(std::vector<uint8_t>& data);

// Remove shadow blend multiply (fixes sun flickering)
PatchResult PatchShadowBlendMultiply(std::vector<uint8_t>& data);

// Remove ClusteredLighting_t from CBufCommonPerCamera (784->752 bytes)
PatchResult PatchRemoveClusteredLighting(std::vector<uint8_t>& data);

// ============================================================================
// Hash Functions
// ============================================================================

void UpdateHash(std::vector<uint8_t>& data);
bool VerifyHash(const std::vector<uint8_t>& data);

// Internal helpers
static uint32_t PatchSHEXSwap(uint8_t* shexData, size_t shexSize);
static uint32_t PatchRDEFSwap(uint8_t* rdefData, size_t rdefSize);

// ============================================================================
// Inline Helpers
// ============================================================================

inline uint32_t GetOpcodeFromToken(uint32_t token) {
    return token & 0x7FF;
}

inline uint32_t GetInstructionLength(uint32_t token) {
    return (token >> 24) & 0x1F;
}

inline uint32_t GetOperandType(uint32_t token) {
    return (token >> 12) & 0xF;
}

inline bool IsExtendedOperand(uint32_t token) {
    return (token >> 31) & 1;
}

inline bool IsCBOperand(uint32_t operandToken) {
    return GetOperandType(operandToken) == OPERAND_TYPE_CONSTANT_BUFFER;
}

inline bool IsResourceOperand(uint32_t operandToken) {
    return GetOperandType(operandToken) == OPERAND_TYPE_RESOURCE;
}

inline size_t FindChunk(const uint8_t* data, size_t size, const char* fourCC) {
    if (size < sizeof(DXBCHeader)) return 0;

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data);
    if (memcmp(header->magic, "DXBC", 4) != 0) return 0;

    uint32_t chunkCount = header->chunkCount;
    if (size < sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t)) return 0;

    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data + sizeof(DXBCHeader));

    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > size) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data + offset);
        if (memcmp(chunk->fourCC, fourCC, 4) == 0) {
            return offset + sizeof(ChunkHeader);
        }
    }
    return 0;
}

inline uint32_t GetChunkSize(const uint8_t* data, size_t size, const char* fourCC) {
    if (size < sizeof(DXBCHeader)) return 0;

    const DXBCHeader* header = reinterpret_cast<const DXBCHeader*>(data);
    if (memcmp(header->magic, "DXBC", 4) != 0) return 0;

    uint32_t chunkCount = header->chunkCount;
    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data + sizeof(DXBCHeader));

    for (uint32_t i = 0; i < chunkCount; i++) {
        uint32_t offset = offsets[i];
        if (offset + sizeof(ChunkHeader) > size) continue;

        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(data + offset);
        if (memcmp(chunk->fourCC, fourCC, 4) == 0) {
            return chunk->size;
        }
    }
    return 0;
}

} // namespace dxbc
