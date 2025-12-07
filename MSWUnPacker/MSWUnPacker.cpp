// MSWUnPacker.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include "multishader.h"
#include "dxbc.h"

#define RAPIDJSON_HAS_STDSTRING 1

#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/document.h"

// Shader type string to enum mapping (for rex-rsx format)
MultiShaderWrapperShaderType_e StringToShaderType(const std::string& typeStr) {
    if (typeStr == "PIXEL") return MultiShaderWrapperShaderType_e::PIXEL;
    if (typeStr == "VERTEX") return MultiShaderWrapperShaderType_e::VERTEX;
    if (typeStr == "GEOMETRY") return MultiShaderWrapperShaderType_e::GEOMETRY;
    if (typeStr == "HULL") return MultiShaderWrapperShaderType_e::HULL;
    if (typeStr == "DOMAIN") return MultiShaderWrapperShaderType_e::DOMAIN;
    if (typeStr == "COMPUTE") return MultiShaderWrapperShaderType_e::COMPUTE;
    return MultiShaderWrapperShaderType_e::PIXEL; // Default
}

// Parse hex string to uint64_t (supports "0x" prefix)
uint64_t ParseHexString(const std::string& hexStr) {
    uint64_t value = 0;
    size_t start = 0;
    if (hexStr.size() >= 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
        start = 2;
    }
    for (size_t i = start; i < hexStr.size(); i++) {
        char c = hexStr[i];
        value <<= 4;
        if (c >= '0' && c <= '9') value |= (c - '0');
        else if (c >= 'a' && c <= 'f') value |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= (c - 'A' + 10);
    }
    return value;
}

// Supported target versions for conversion
constexpr int SHADER_VERSION_LEGACY = 12;      // r5sdk compatible (S3)
constexpr int SHADER_VERSION_CURRENT = 15;     // latest

constexpr int SHADERSET_VERSION_LEGACY = 11;   // r5sdk compatible (S3)
constexpr int SHADERSET_VERSION_S9 = 12;       // S9-S10
constexpr int SHADERSET_VERSION_CURRENT = 14;  // latest

// Source version descriptions
const char* GetShaderVersionDesc(int ver) {
    switch (ver) {
        case 8: return "v8 (legacy)";
        case 12: return "v12 (r5sdk/S3)";
        case 13: return "v13 (S9)";
        case 14: return "v14 (S10)";
        case 15: return "v15 (current)";
        case 16: return "v16 (latest)";
        default: return "unknown";
    }
}

const char* GetShaderSetVersionDesc(int ver) {
    switch (ver) {
        case 8: return "v8 (legacy)";
        case 11: return "v11 (r5sdk/S3)";
        case 12: return "v12 (S9-S10)";
        case 13: return "v13 (S10+)";
        case 14: return "v14 (current)";
        default: return "unknown";
    }
}

// Calculate shader count using v12 algorithm (7-byte features)
int CalculateShaderCount_v12(const unsigned char features[7]) {
    if (features[0] != 0xFF) {
        return features[0];
    }

    int v6 = 2 * ((features[1] != 0) + 1);
    if (!features[2])
        v6 = (features[1] != 0) + 1;

    int v7 = v6;
    if (features[3])
        v7 *= 2;

    int numShaders = 2 * v7;
    if (!features[6])
        numShaders = v7;

    return numShaders;
}

// Calculate shader count using v15 algorithm (9-byte features, but we only have 7)
// This approximates based on available data
int CalculateShaderCount_v15(const unsigned char features[7]) {
    int shaderCount = features[0];
    if (shaderCount == 0xFF) {
        shaderCount = (features[1] != 0) + 1;
        if (features[2]) shaderCount *= 2;
        if (features[3]) shaderCount *= 2;
        if (features[4]) shaderCount *= 2;
        // features[7] would be used but we only have 7 bytes
        if (features[6]) shaderCount *= 2;
    }
    return shaderCount;
}

void unpack(const char* path) {
    CMultiShaderWrapperIO::ShaderCache_t shaderCache = {};
    MSW_ParseFile(path,shaderCache);
    switch (shaderCache.type) {
    case MultiShaderWrapperFileType_e::SHADER:
    {
        // Use filename as output directory if shader has no embedded name
        std::string outputDir = shaderCache.shader->name;
        if (outputDir.empty()) {
            fs::path inputPath(path);
            outputDir = inputPath.stem().string();  // Get filename without extension
            shaderCache.shader->name = outputDir;   // Also set for JSON output
        }
        fs::create_directories(outputDir);
        for (size_t i = 0; i < shaderCache.shader->entries.size(); i++) {

            auto& entry = shaderCache.shader->entries[i];
            if(!entry.buffer)continue;
            std::ofstream out{ std::format("{}/{}.fxc",outputDir,i).c_str(),std::ios::binary };
            out.write(entry.buffer, entry.size);
            out.close();
        }
        rapidjson::StringBuffer jsonBuf{};
        rapidjson::PrettyWriter<rapidjson::StringBuffer> jsonWriter{jsonBuf};
        jsonWriter.StartObject();
        jsonWriter.Key("type");
        jsonWriter.Int((int)MultiShaderWrapperFileType_e::SHADER);
        jsonWriter.Key("shaderType");
        jsonWriter.Int((int)shaderCache.shader->shaderType);
        jsonWriter.Key("name");
        jsonWriter.String(shaderCache.shader->name);
        jsonWriter.Key("features");
        jsonWriter.StartArray();
        for (int i = 0; i < 7; i++) {
            jsonWriter.Int(shaderCache.shader->features[i]);
        }
        jsonWriter.EndArray();
        jsonWriter.Key("entryFlags");
        jsonWriter.StartArray();
        for (auto& entry : shaderCache.shader->entries) {
            jsonWriter.StartArray();
            for (int i = 0; i < 2; i++) {
                jsonWriter.Uint64(entry.flags[i]);
            }
            jsonWriter.EndArray();
        }
        jsonWriter.EndArray();
        jsonWriter.Key("entryRefs");
        jsonWriter.StartObject();
        for (size_t i = 0; i < shaderCache.shader->entries.size(); i++) {
            if ((!shaderCache.shader->entries[i].buffer)&&shaderCache.shader->entries[i].refIndex!=0xFFFF) {
                char key[4];
                snprintf(key,4,"%d",i);
                jsonWriter.Key(key);
                jsonWriter.Int(shaderCache.shader->entries[i].refIndex);
            }
        }
        jsonWriter.EndObject();
        jsonWriter.EndObject();
        std::ofstream jsonOut{ std::format("{}/data.json",outputDir).c_str() };
        jsonOut.write(jsonBuf.GetString(), jsonBuf.GetSize());
        jsonOut.close();
    }
        break;
    case MultiShaderWrapperFileType_e::SHADERSET:
    {
        // Use filename as output directory for shader sets
        fs::path inputPath(path);
        std::string shaderSetDir = inputPath.stem().string();
        fs::create_directories(shaderSetDir);

        rapidjson::StringBuffer jsonBuf{};
        rapidjson::PrettyWriter<rapidjson::StringBuffer> jsonWriter{jsonBuf};
        jsonWriter.StartObject();
        jsonWriter.Key("type");
        jsonWriter.Int((int)MultiShaderWrapperFileType_e::SHADERSET);
        jsonWriter.Key("pixelShaderGuid");
        jsonWriter.Uint64(shaderCache.shaderSet.pixelShaderGuid);
        jsonWriter.Key("vertexShaderGuid");
        jsonWriter.Uint64(shaderCache.shaderSet.vertexShaderGuid);
        jsonWriter.Key("numPixelShaderTextures");
        jsonWriter.Uint(shaderCache.shaderSet.numPixelShaderTextures);
        jsonWriter.Key("numVertexShaderTextures");
        jsonWriter.Uint(shaderCache.shaderSet.numVertexShaderTextures);
        jsonWriter.Key("numSamplers");
        jsonWriter.Uint(shaderCache.shaderSet.numSamplers);
        jsonWriter.Key("firstResourceBindPoint");
        jsonWriter.Uint(shaderCache.shaderSet.firstResourceBindPoint);
        jsonWriter.Key("numResources");
        jsonWriter.Uint(shaderCache.shaderSet.numResources);
        jsonWriter.EndObject();
        std::ofstream jsonOut{ std::format("{}/data.json",shaderSetDir).c_str() };
        jsonOut.write(jsonBuf.GetString(), jsonBuf.GetSize());
        jsonOut.close();
    }
        break;
    }
}

void pack(const char* path) {
    printf("creating Shader\n");
    std::ifstream jsonFile(fs::path(path)/"data.json");
    std::stringstream jsonFileStream;
    rapidjson::Document jsonDoc;
    if (jsonFile.fail())return;
    
    while (jsonFile.peek() != EOF)
        jsonFileStream << (char)jsonFile.get();

    jsonFile.close();
    jsonDoc.Parse<rapidjson::ParseFlag::kParseCommentsFlag | rapidjson::ParseFlag::kParseTrailingCommasFlag>(
        jsonFileStream.str().c_str());
    if(!jsonDoc.IsObject())return;
    if((!jsonDoc.HasMember("type"))||(!jsonDoc["type"].IsInt()))return;
    switch ((MultiShaderWrapperFileType_e)jsonDoc["type"].GetInt()) {
    case MultiShaderWrapperFileType_e::SHADER:
    {
        bool isValid = true;
        CMultiShaderWrapperIO::Shader_t shader;
        if((!jsonDoc.HasMember("shaderType"))||(!jsonDoc["shaderType"].IsInt()))return;
        shader.shaderType = (MultiShaderWrapperShaderType_e)jsonDoc["shaderType"].GetInt();
        if (jsonDoc.HasMember("name") && jsonDoc["name"].IsString()) {
            shader.name = jsonDoc["name"].GetString();
            printf("Shader will be exported as %s.msw\n",shader.name.c_str());
        }
        else {
            isValid = false;
            fprintf(stderr,"Field \"name\" missing or wrong format\n");
        }
        

        if (jsonDoc.HasMember("features") && jsonDoc["features"].IsArray()) {
            auto features = jsonDoc["features"].GetArray();
            if (features.Size() == 7) {
                for (int i = 0; i < 7; i++) {
                    if (!features[i].IsInt()) {
                        isValid = false;
                        fprintf(stderr,"Index %d in \"Features\" wrong format\n",i);
                        break;
                    }
                    shader.features[i] = features[i].GetInt();
                }
            }
            else {
                fprintf(stderr,"\"Features\" has the wrong amount of fields should be 7 is %d\n",features.Size());
                isValid = false;
            }
            
        }
        else {
            fprintf(stderr,"Field \"features\" missing or wrong format\n");
        }
        
        if((!jsonDoc.HasMember("entryFlags"))||(!jsonDoc["entryFlags"].IsArray()))return;
        
         
        auto entrys = jsonDoc["entryFlags"].GetArray();
        for (size_t i = 0; i < entrys.Size(); i++) {
            CMultiShaderWrapperIO::ShaderEntry_t& ent = shader.entries.emplace_back();
            ent.refIndex = 0xFFFF;
            if (jsonDoc.HasMember("entryRefs") && jsonDoc["entryRefs"].IsObject()) {
                char key[4];
                snprintf(key,4,"%d",i);
                if(jsonDoc["entryRefs"].HasMember(key))
                    if(jsonDoc["entryRefs"][key].IsInt())
                        ent.refIndex = jsonDoc["entryRefs"][key].GetInt();
            }
            if (ent.refIndex == 0xFFFF) {
                std::ifstream shaderFile{std::format("{}/{}.fxc",path,i),std::ios::binary};
                if (!shaderFile.fail()) {
                    shaderFile.seekg(0,std::ios::end);
                    ent.size = shaderFile.tellg();
                    char* buf = new char[ent.size];
                    shaderFile.seekg(0,std::ios::beg);
                    shaderFile.read(buf,ent.size);
                    ent.buffer = buf;
                }
                else {
                    ent.buffer = 0;
                    ent.size = 0;
                }
                
            }
            else {
                ent.buffer = 0;
                ent.size = 0;
            }
            
            if(!entrys[i].IsArray())return;
            for (int j = 0; j < 2; j++) {
                if(!entrys[i][j].IsUint64())return;
                ent.flags[j] = entrys[i][j].GetUint64();
            }
            
        }
        CMultiShaderWrapperIO writer{};
        writer.SetFileType(MultiShaderWrapperFileType_e::SHADER);
        writer.SetShader(&shader);
        writer.WriteFile(std::format("{}.msw",path).c_str());
    }
        break;
    case MultiShaderWrapperFileType_e::SHADERSET:
    {
        bool isValid = true;
        CMultiShaderWrapperIO::ShaderSet_t shaderSet;
        printf("Creating shaderset");
        if (jsonDoc.HasMember("pixelShaderGuid") && jsonDoc["pixelShaderGuid"].IsUint64()) {
            shaderSet.pixelShaderGuid = jsonDoc["pixelShaderGuid"].GetUint64();
        }
        else if (jsonDoc.HasMember("pixelShaderName") && jsonDoc["pixelShaderName"].IsString()) {

        }
        else {
            fprintf(stderr,"No PixelShader Guid or Name Given\n");
            isValid = false;
        }
        if (jsonDoc.HasMember("vertexShaderGuid") && jsonDoc["vertexShaderGuid"].IsUint64()) {
            shaderSet.pixelShaderGuid = jsonDoc["vertexShaderGuid"].GetUint64();
        }
        else if (jsonDoc.HasMember("vertexShaderName") && jsonDoc["vertexShaderName"].IsString()) {

        }
        else {
            fprintf(stderr,"No PixelShader Guid or Name Given\n");
            isValid = false;
        }

        if (jsonDoc.HasMember("numPixelShaderTextures")&& jsonDoc["numPixelShaderTextures"].IsUint()) {
            shaderSet.numPixelShaderTextures = jsonDoc["numPixelShaderTextures"].GetUint();
        }
        else {
            isValid = false;
            fprintf(stderr,"Field \"numPixelShaderTextures\" missing or wrong format\n");
        }

        if (jsonDoc.HasMember("numVertexShaderTextures")&& jsonDoc["numVertexShaderTextures"].IsUint()) {
            shaderSet.numVertexShaderTextures = jsonDoc["numVertexShaderTextures"].GetUint();
        }
        else {
            isValid = false;
            fprintf(stderr,"Field \"numVertexShaderTextures\" missing or wrong format\n");
        }

        if (jsonDoc.HasMember("numSamplers")&& jsonDoc["numSamplers"].IsUint()) {
            shaderSet.numSamplers = jsonDoc["numSamplers"].GetUint();
        }
        else {
            isValid = false;
            fprintf(stderr,"Field \"numSamplers\" missing or wrong format\n");
        }

        if (jsonDoc.HasMember("firstResourceBindPoint")&& jsonDoc["firstResourceBindPoint"].IsUint()) {
            shaderSet.firstResourceBindPoint = jsonDoc["firstResourceBindPoint"].GetUint();
        }
        else {
            isValid = false;
            fprintf(stderr,"Field \"firstResourceBindPoint\" missing or wrong format\n");
        }

        if (jsonDoc.HasMember("numResources")&& jsonDoc["numResources"].IsUint()) {
            shaderSet.numResources = jsonDoc["numResources"].GetUint();
        }
        else {
            isValid = false;
            fprintf(stderr,"Field \"numResources\" missing or wrong format\n");
        }

        if (!isValid) {
            printf("Did not write file since there are errors");
        }
        CMultiShaderWrapperIO writer{};
        writer.SetFileType(MultiShaderWrapperFileType_e::SHADERSET);
        writer.SetShaderSet(shaderSet);
        writer.WriteFile(std::format("{}.msw",path).c_str());
    }
        break;
    default:
        printf("unknown File Type\n");
    }
}

// Convert shader/shaderset data.json to target version format
// This modifies the JSON to include version info and adjusts fields for compatibility
void convert(const char* path, int targetShaderVersion, int targetShaderSetVersion) {
    printf("Converting to shader v%d, shaderset v%d\n", targetShaderVersion, targetShaderSetVersion);

    std::ifstream jsonFile(fs::path(path) / "data.json");
    std::stringstream jsonFileStream;
    rapidjson::Document jsonDoc;

    if (jsonFile.fail()) {
        fprintf(stderr, "Error: Could not open %s/data.json\n", path);
        return;
    }

    while (jsonFile.peek() != EOF)
        jsonFileStream << (char)jsonFile.get();
    jsonFile.close();

    jsonDoc.Parse<rapidjson::ParseFlag::kParseCommentsFlag | rapidjson::ParseFlag::kParseTrailingCommasFlag>(
        jsonFileStream.str().c_str());

    if (!jsonDoc.IsObject()) {
        fprintf(stderr, "Error: Invalid JSON format\n");
        return;
    }

    if (!jsonDoc.HasMember("type") || !jsonDoc["type"].IsInt()) {
        fprintf(stderr, "Error: Missing or invalid 'type' field\n");
        return;
    }

    MultiShaderWrapperFileType_e fileType = (MultiShaderWrapperFileType_e)jsonDoc["type"].GetInt();

    // Prepare output JSON
    rapidjson::StringBuffer jsonBuf{};
    rapidjson::PrettyWriter<rapidjson::StringBuffer> jsonWriter{jsonBuf};
    jsonWriter.StartObject();

    switch (fileType) {
    case MultiShaderWrapperFileType_e::SHADER:
    {
        printf("Converting SHADER asset...\n");

        // Copy type
        jsonWriter.Key("type");
        jsonWriter.Int((int)fileType);

        // Add target version
        jsonWriter.Key("targetVersion");
        jsonWriter.Int(targetShaderVersion);

        // Copy shaderType
        if (jsonDoc.HasMember("shaderType") && jsonDoc["shaderType"].IsInt()) {
            int shaderType = jsonDoc["shaderType"].GetInt();
            // Validate shader type for legacy version
            if (targetShaderVersion <= 12 && shaderType > 5) {
                fprintf(stderr, "Warning: Shader type %d may not be supported in v%d\n",
                        shaderType, targetShaderVersion);
            }
            jsonWriter.Key("shaderType");
            jsonWriter.Int(shaderType);
        }

        // Copy name
        if (jsonDoc.HasMember("name") && jsonDoc["name"].IsString()) {
            jsonWriter.Key("name");
            jsonWriter.String(jsonDoc["name"].GetString());
        }

        // Process features array
        unsigned char features[8] = {0};
        if (jsonDoc.HasMember("features") && jsonDoc["features"].IsArray()) {
            auto featuresArr = jsonDoc["features"].GetArray();
            for (rapidjson::SizeType i = 0; i < featuresArr.Size() && i < 8; i++) {
                if (featuresArr[i].IsInt()) {
                    features[i] = (unsigned char)featuresArr[i].GetInt();
                }
            }
        }

        // Calculate actual shader count from entryFlags
        int actualShaderCount = 0;
        if (jsonDoc.HasMember("entryFlags") && jsonDoc["entryFlags"].IsArray()) {
            actualShaderCount = (int)jsonDoc["entryFlags"].GetArray().Size();
        }

        // For v12, we need to ensure the calculated count matches actual count
        // If not, force direct count mode by setting features[0]
        int calculatedCount = CalculateShaderCount_v12(features);
        if (calculatedCount != actualShaderCount && targetShaderVersion == 12) {
            printf("Shader count mismatch: calculated=%d, actual=%d\n", calculatedCount, actualShaderCount);
            if (actualShaderCount <= 255) {
                printf("Forcing direct count mode: features[0] = %d\n", actualShaderCount);
                features[0] = (unsigned char)actualShaderCount;
            } else {
                fprintf(stderr, "Warning: Shader count %d exceeds v12 direct count limit (255)\n", actualShaderCount);
            }
        }

        // Write features (always 7 bytes for MSW format)
        jsonWriter.Key("features");
        jsonWriter.StartArray();
        for (int i = 0; i < 7; i++) {
            jsonWriter.Int(features[i]);
        }
        jsonWriter.EndArray();

        // Add explicit shader count for verification
        jsonWriter.Key("shaderCount");
        jsonWriter.Int(actualShaderCount);

        // Copy entryFlags
        if (jsonDoc.HasMember("entryFlags") && jsonDoc["entryFlags"].IsArray()) {
            jsonWriter.Key("entryFlags");
            jsonWriter.StartArray();
            for (auto& entry : jsonDoc["entryFlags"].GetArray()) {
                jsonWriter.StartArray();
                if (entry.IsArray()) {
                    for (auto& flag : entry.GetArray()) {
                        if (flag.IsUint64()) {
                            jsonWriter.Uint64(flag.GetUint64());
                        }
                    }
                }
                jsonWriter.EndArray();
            }
            jsonWriter.EndArray();
        }

        // Copy entryRefs
        if (jsonDoc.HasMember("entryRefs") && jsonDoc["entryRefs"].IsObject()) {
            jsonWriter.Key("entryRefs");
            jsonWriter.StartObject();
            for (auto& m : jsonDoc["entryRefs"].GetObject()) {
                jsonWriter.Key(m.name.GetString());
                if (m.value.IsInt()) {
                    jsonWriter.Int(m.value.GetInt());
                }
            }
            jsonWriter.EndObject();
        }

        printf("Shader conversion complete. Count: %d\n", actualShaderCount);
    }
        break;

    case MultiShaderWrapperFileType_e::SHADERSET:
    {
        printf("Converting SHADERSET asset...\n");

        // Detect source version from existing fields
        int sourceVersion = SHADERSET_VERSION_CURRENT; // Default to v14
        if (jsonDoc.HasMember("sourceVersion") && jsonDoc["sourceVersion"].IsInt()) {
            sourceVersion = jsonDoc["sourceVersion"].GetInt();
        } else if (jsonDoc.HasMember("vsInputLayoutIds")) {
            // If vsInputLayoutIds exists, it's v11, v12, or v13
            auto ids = jsonDoc["vsInputLayoutIds"].GetArray();
            if (ids.Size() == 16) sourceVersion = SHADERSET_VERSION_LEGACY;  // v11
            else if (ids.Size() == 32) sourceVersion = SHADERSET_VERSION_S9; // v12
        }
        printf("Source version: %s -> Target: %s\n",
               GetShaderSetVersionDesc(sourceVersion),
               GetShaderSetVersionDesc(targetShaderSetVersion));

        // Copy type
        jsonWriter.Key("type");
        jsonWriter.Int((int)fileType);

        // Add source and target version
        jsonWriter.Key("sourceVersion");
        jsonWriter.Int(sourceVersion);
        jsonWriter.Key("targetVersion");
        jsonWriter.Int(targetShaderSetVersion);

        // Copy GUIDs
        if (jsonDoc.HasMember("pixelShaderGuid") && jsonDoc["pixelShaderGuid"].IsUint64()) {
            jsonWriter.Key("pixelShaderGuid");
            jsonWriter.Uint64(jsonDoc["pixelShaderGuid"].GetUint64());
        }

        if (jsonDoc.HasMember("vertexShaderGuid") && jsonDoc["vertexShaderGuid"].IsUint64()) {
            jsonWriter.Key("vertexShaderGuid");
            jsonWriter.Uint64(jsonDoc["vertexShaderGuid"].GetUint64());
        }

        // Copy texture counts
        // All versions v11+: textureInputCounts[0] = PS textures, [1] = total (PS+VS)
        // numVertexShaderTextures = textureInputCounts[1] - textureInputCounts[0]
        if (jsonDoc.HasMember("numPixelShaderTextures") && jsonDoc["numPixelShaderTextures"].IsUint()) {
            jsonWriter.Key("numPixelShaderTextures");
            jsonWriter.Uint(jsonDoc["numPixelShaderTextures"].GetUint());
        }

        if (jsonDoc.HasMember("numVertexShaderTextures") && jsonDoc["numVertexShaderTextures"].IsUint()) {
            jsonWriter.Key("numVertexShaderTextures");
            jsonWriter.Uint(jsonDoc["numVertexShaderTextures"].GetUint());
        }

        if (jsonDoc.HasMember("numSamplers") && jsonDoc["numSamplers"].IsUint()) {
            jsonWriter.Key("numSamplers");
            jsonWriter.Uint(jsonDoc["numSamplers"].GetUint());
        }

        if (jsonDoc.HasMember("firstResourceBindPoint") && jsonDoc["firstResourceBindPoint"].IsUint()) {
            jsonWriter.Key("firstResourceBindPoint");
            jsonWriter.Uint(jsonDoc["firstResourceBindPoint"].GetUint());
        }

        if (jsonDoc.HasMember("numResources") && jsonDoc["numResources"].IsUint()) {
            jsonWriter.Key("numResources");
            jsonWriter.Uint(jsonDoc["numResources"].GetUint());
        }

        // Handle vsInputLayoutIds based on source and target versions
        // v11: 16 bytes, v12/v13: 32 bytes, v14: none
        if (targetShaderSetVersion == SHADERSET_VERSION_LEGACY) {
            // Target is v11 - need 16 bytes of vsInputLayoutIds
            jsonWriter.Key("vsInputLayoutIds");
            jsonWriter.StartArray();

            if (sourceVersion == SHADERSET_VERSION_S9 || sourceVersion == 13) {
                // Source is v12/v13 with 32 bytes - truncate to 16
                if (jsonDoc.HasMember("vsInputLayoutIds") && jsonDoc["vsInputLayoutIds"].IsArray()) {
                    auto ids = jsonDoc["vsInputLayoutIds"].GetArray();
                    for (int i = 0; i < 16 && i < (int)ids.Size(); i++) {
                        jsonWriter.Int(ids[i].IsInt() ? ids[i].GetInt() : 255);
                    }
                    // Pad if source had fewer
                    for (int i = (int)ids.Size(); i < 16; i++) {
                        jsonWriter.Int(255);
                    }
                    printf("Truncated vsInputLayoutIds: 32 -> 16 bytes (v12/v13 -> v11)\n");
                } else {
                    // No source IDs, fill with 0xFF
                    for (int i = 0; i < 16; i++) jsonWriter.Int(255);
                }
            } else if (sourceVersion == SHADERSET_VERSION_CURRENT) {
                // Source is v14 - no vsInputLayoutIds, generate defaults
                for (int i = 0; i < 16; i++) {
                    jsonWriter.Int(255); // 0xFF = use shader's default layout
                }
                printf("Generated vsInputLayoutIds for v14 -> v11 conversion\n");
            } else if (sourceVersion == SHADERSET_VERSION_LEGACY) {
                // Source is already v11, copy existing
                if (jsonDoc.HasMember("vsInputLayoutIds") && jsonDoc["vsInputLayoutIds"].IsArray()) {
                    auto ids = jsonDoc["vsInputLayoutIds"].GetArray();
                    for (int i = 0; i < 16; i++) {
                        jsonWriter.Int(i < (int)ids.Size() && ids[i].IsInt() ? ids[i].GetInt() : 255);
                    }
                } else {
                    for (int i = 0; i < 16; i++) jsonWriter.Int(255);
                }
            } else {
                // Unknown source, use defaults
                for (int i = 0; i < 16; i++) jsonWriter.Int(255);
            }
            jsonWriter.EndArray();
        } else if (targetShaderSetVersion == SHADERSET_VERSION_S9) {
            // Target is v12 - need 32 bytes of vsInputLayoutIds
            jsonWriter.Key("vsInputLayoutIds");
            jsonWriter.StartArray();

            if (jsonDoc.HasMember("vsInputLayoutIds") && jsonDoc["vsInputLayoutIds"].IsArray()) {
                auto ids = jsonDoc["vsInputLayoutIds"].GetArray();
                for (int i = 0; i < 32; i++) {
                    jsonWriter.Int(i < (int)ids.Size() && ids[i].IsInt() ? ids[i].GetInt() : 255);
                }
            } else {
                for (int i = 0; i < 32; i++) jsonWriter.Int(255);
            }
            jsonWriter.EndArray();
            printf("Set vsInputLayoutIds for v12 (32 bytes)\n");
        }
        // v14 target doesn't need vsInputLayoutIds

        printf("ShaderSet conversion complete: %s -> %s\n",
               GetShaderSetVersionDesc(sourceVersion),
               GetShaderSetVersionDesc(targetShaderSetVersion));
    }
        break;

    default:
        fprintf(stderr, "Error: Unknown file type %d\n", (int)fileType);
        return;
    }

    jsonWriter.EndObject();

    // Write converted JSON
    std::string outputPath = std::string(path) + "/data.json";
    std::ofstream jsonOut(outputPath);
    if (jsonOut.fail()) {
        fprintf(stderr, "Error: Could not write to %s\n", outputPath.c_str());
        return;
    }
    jsonOut.write(jsonBuf.GetString(), jsonBuf.GetSize());
    jsonOut.close();

    printf("Written converted data to %s\n", outputPath.c_str());
}

// Convert rex-rsx exported shader JSON to MSWUnPacker format
// Input: rex-rsx JSON file (e.g., 0x3E3A804D0F4109F2.json)
// Output: MSWUnPacker data.json in a new directory
void convertRsx(const char* inputJsonPath, const char* outputDir, int targetVersion) {
    printf("Converting rex-rsx shader to MSWUnPacker format (target: v%d)\n", targetVersion);
    printf("Input: %s\n", inputJsonPath);
    printf("Output: %s\n", outputDir);

    // Read input JSON
    std::ifstream jsonFile(inputJsonPath);
    if (jsonFile.fail()) {
        fprintf(stderr, "Error: Could not open %s\n", inputJsonPath);
        return;
    }

    std::stringstream jsonFileStream;
    while (jsonFile.peek() != EOF)
        jsonFileStream << (char)jsonFile.get();
    jsonFile.close();

    rapidjson::Document jsonDoc;
    jsonDoc.Parse<rapidjson::ParseFlag::kParseCommentsFlag | rapidjson::ParseFlag::kParseTrailingCommasFlag>(
        jsonFileStream.str().c_str());

    if (!jsonDoc.IsObject()) {
        fprintf(stderr, "Error: Invalid JSON format\n");
        return;
    }

    // Parse rex-rsx format
    // type: "PIXEL", "VERTEX", etc.
    MultiShaderWrapperShaderType_e shaderType = MultiShaderWrapperShaderType_e::PIXEL;
    if (jsonDoc.HasMember("type") && jsonDoc["type"].IsString()) {
        shaderType = StringToShaderType(jsonDoc["type"].GetString());
        printf("Shader type: %s (%d)\n", jsonDoc["type"].GetString(), (int)shaderType);
    }

    // features: hex string like "0" or "FF01020304050607"
    // This is 7 bytes (56 bits) of features
    unsigned char features[7] = {0};
    if (jsonDoc.HasMember("features") && jsonDoc["features"].IsString()) {
        uint64_t featuresVal = ParseHexString(jsonDoc["features"].GetString());
        printf("Features value: 0x%llX\n", featuresVal);
        // Extract 7 bytes from the value (little-endian in memory)
        for (int i = 0; i < 7; i++) {
            features[i] = (unsigned char)((featuresVal >> (i * 8)) & 0xFF);
        }
    }

    // inputFlags: array of hex strings, pairs per shader entry
    // ["0x...", "0x...", "0x...", "0x..."] for 2 shader entries
    std::vector<std::pair<uint64_t, uint64_t>> entryFlags;
    if (jsonDoc.HasMember("inputFlags") && jsonDoc["inputFlags"].IsArray()) {
        auto flags = jsonDoc["inputFlags"].GetArray();
        // Flags come in pairs
        for (rapidjson::SizeType i = 0; i + 1 < flags.Size(); i += 2) {
            uint64_t flag0 = 0, flag1 = 0;
            if (flags[i].IsString()) {
                flag0 = ParseHexString(flags[i].GetString());
            }
            if (flags[i + 1].IsString()) {
                flag1 = ParseHexString(flags[i + 1].GetString());
            }
            entryFlags.push_back({flag0, flag1});
        }
        printf("Parsed %zu shader entries from inputFlags\n", entryFlags.size());
    }

    // refIndices: array of integers (65535 = 0xFFFF = no ref)
    std::vector<uint16_t> refIndices;
    if (jsonDoc.HasMember("refIndices") && jsonDoc["refIndices"].IsArray()) {
        for (auto& ref : jsonDoc["refIndices"].GetArray()) {
            if (ref.IsInt() || ref.IsUint()) {
                refIndices.push_back((uint16_t)ref.GetUint());
            }
        }
    }

    // Validate entry count matches ref count
    if (!refIndices.empty() && refIndices.size() != entryFlags.size()) {
        printf("Warning: refIndices count (%zu) != entryFlags count (%zu)\n",
               refIndices.size(), entryFlags.size());
        // Pad refIndices if needed
        while (refIndices.size() < entryFlags.size()) {
            refIndices.push_back(0xFFFF);
        }
    }

    // For v12 conversion, adjust features[0] to store direct shader count if needed
    int actualShaderCount = (int)entryFlags.size();
    if (targetVersion == SHADER_VERSION_LEGACY) {
        int calculatedCount = CalculateShaderCount_v12(features);
        if (calculatedCount != actualShaderCount) {
            printf("Shader count mismatch: calculated=%d, actual=%d\n", calculatedCount, actualShaderCount);
            if (actualShaderCount <= 255) {
                printf("Setting direct count mode: features[0] = %d\n", actualShaderCount);
                features[0] = (unsigned char)actualShaderCount;
            } else {
                fprintf(stderr, "Warning: Shader count %d exceeds v12 direct count limit (255)\n", actualShaderCount);
            }
        }
    }

    // Create output directory
    fs::path outPath(outputDir);
    fs::create_directories(outPath);

    // Get shader name from input filename
    fs::path inputPath(inputJsonPath);
    std::string shaderName = inputPath.stem().string();

    // Write MSWUnPacker format data.json
    rapidjson::StringBuffer jsonBuf{};
    rapidjson::PrettyWriter<rapidjson::StringBuffer> jsonWriter{jsonBuf};
    jsonWriter.StartObject();

    jsonWriter.Key("type");
    jsonWriter.Int((int)MultiShaderWrapperFileType_e::SHADER);

    jsonWriter.Key("shaderType");
    jsonWriter.Int((int)shaderType);

    jsonWriter.Key("name");
    jsonWriter.String(shaderName);

    jsonWriter.Key("targetVersion");
    jsonWriter.Int(targetVersion);

    jsonWriter.Key("features");
    jsonWriter.StartArray();
    for (int i = 0; i < 7; i++) {
        jsonWriter.Int(features[i]);
    }
    jsonWriter.EndArray();

    jsonWriter.Key("entryFlags");
    jsonWriter.StartArray();
    for (const auto& entry : entryFlags) {
        jsonWriter.StartArray();
        jsonWriter.Uint64(entry.first);
        jsonWriter.Uint64(entry.second);
        jsonWriter.EndArray();
    }
    jsonWriter.EndArray();

    jsonWriter.Key("entryRefs");
    jsonWriter.StartObject();
    for (size_t i = 0; i < refIndices.size(); i++) {
        if (refIndices[i] != 0xFFFF) {
            char key[8];
            snprintf(key, sizeof(key), "%zu", i);
            jsonWriter.Key(key);
            jsonWriter.Int(refIndices[i]);
        }
    }
    jsonWriter.EndObject();

    jsonWriter.EndObject();

    // Write output JSON
    std::string outputJsonPath = (outPath / "data.json").string();
    std::ofstream jsonOut(outputJsonPath);
    if (jsonOut.fail()) {
        fprintf(stderr, "Error: Could not write to %s\n", outputJsonPath.c_str());
        return;
    }
    jsonOut.write(jsonBuf.GetString(), jsonBuf.GetSize());
    jsonOut.close();

    printf("Written: %s\n", outputJsonPath.c_str());

    // Copy FXC files from source directory
    fs::path sourceDir = inputPath.parent_path();
    std::string baseName = inputPath.stem().string();

    int fxcCount = 0;
    std::vector<int> missingFxcEntries; // Track entries that need references

    for (int i = 0; i < (int)entryFlags.size(); i++) {
        // Rex-rsx names FXC files as: {guid}_{index}.fxc
        std::string srcFxcName = baseName + "_" + std::to_string(i) + ".fxc";
        fs::path srcFxc = sourceDir / srcFxcName;

        // MSWUnPacker expects: {index}.fxc
        std::string dstFxcName = std::to_string(i) + ".fxc";
        fs::path dstFxc = outPath / dstFxcName;

        if (fs::exists(srcFxc)) {
            try {
                fs::copy_file(srcFxc, dstFxc, fs::copy_options::overwrite_existing);
                printf("Copied: %s -> %s\n", srcFxcName.c_str(), dstFxcName.c_str());
                fxcCount++;
            } catch (const fs::filesystem_error& e) {
                fprintf(stderr, "Error copying %s: %s\n", srcFxcName.c_str(), e.what());
            }
        } else {
            // Check if this entry has a refIndex (references another shader's bytecode)
            if (i < (int)refIndices.size() && refIndices[i] != 0xFFFF) {
                printf("Entry %d references entry %d (no FXC file needed)\n", i, refIndices[i]);
            } else {
                // Rex-rsx may have deduplicated bytecode - track for reference assignment
                missingFxcEntries.push_back(i);
            }
        }
    }

    // Handle missing FXC files by setting references to the first available entry
    if (!missingFxcEntries.empty() && fxcCount > 0) {
        printf("Note: %zu entries missing FXC files - setting as references to entry 0\n", missingFxcEntries.size());

        // Re-read the output JSON to add entryRefs
        std::ifstream inJson(outputJsonPath);
        std::stringstream ss;
        while (inJson.peek() != EOF) ss << (char)inJson.get();
        inJson.close();

        rapidjson::Document outDoc;
        outDoc.Parse(ss.str().c_str());

        // Update entryRefs
        if (outDoc.HasMember("entryRefs") && outDoc["entryRefs"].IsObject()) {
            for (int idx : missingFxcEntries) {
                char key[8];
                snprintf(key, sizeof(key), "%d", idx);
                rapidjson::Value keyVal(key, outDoc.GetAllocator());
                outDoc["entryRefs"].AddMember(keyVal, 0, outDoc.GetAllocator()); // Reference entry 0
                printf("  Entry %d -> references entry 0\n", idx);
            }
        }

        // Write updated JSON
        rapidjson::StringBuffer sb;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
        outDoc.Accept(writer);

        std::ofstream outJson(outputJsonPath);
        outJson.write(sb.GetString(), sb.GetSize());
        outJson.close();
    } else if (!missingFxcEntries.empty()) {
        for (int idx : missingFxcEntries) {
            fprintf(stderr, "Warning: Missing FXC file for entry %d: %s_%d.fxc\n", idx, baseName.c_str(), idx);
        }
    }

    printf("\nConversion complete!\n");
    printf("  Shader entries: %d\n", actualShaderCount);
    printf("  FXC files copied: %d\n", fxcCount);
    printf("  Output directory: %s\n", outputDir);
    printf("\nNext steps:\n");
    printf("  1. MSWUnPacker pack %s   (to create .msw file)\n", outputDir);
    printf("  2. Use RePak to create r5sdk-compatible rpak\n");
}

// Convert S9 shader to legacy format with automatic CB2<->CB3 swap detection
// This is the integrated version of the shader_patcher workflow
// Returns: 0 = converted, 1 = used S7, -1 = error
int convertLegacy(const char* inputPath, const char* outputPath) {
    printf("=== S9 to Legacy Shader Converter ===\n");
    printf("Input: %s\n", inputPath);

    fs::path input(inputPath);
    fs::path tempDir;
    fs::path outputMsw;
    bool createdTempDir = false;

    // Determine if input is MSW file or directory
    bool isMswFile = false;
    if (fs::is_regular_file(input)) {
        std::string ext = input.extension().string();
        for (char& c : ext) c = static_cast<char>(tolower(c));
        if (ext == ".msw") {
            isMswFile = true;
        }
    }

    // Extract shader GUID from input path
    std::string shaderGuid = input.stem().string();

    // Set up output path first (needed for S7 check)
    if (isMswFile) {
        if (outputPath && strlen(outputPath) > 0) {
            outputMsw = fs::path(outputPath);
        } else {
            outputMsw = input.parent_path() / (input.stem().string() + "_legacy.msw");
        }
    } else if (fs::is_directory(input)) {
        if (outputPath && strlen(outputPath) > 0) {
            outputMsw = fs::path(outputPath);
        } else {
            outputMsw = fs::path(inputPath).string() + "_legacy.msw";
        }
    } else {
        fprintf(stderr, "Error: Input must be an MSW file or directory\n");
        return -1;
    }

    // Set up working directory
    if (isMswFile) {
        // Unpack MSW to temp directory
        tempDir = input.stem();
        printf("Unpacking MSW to: %s\n", tempDir.string().c_str());
        unpack(inputPath);
        createdTempDir = true;
    } else if (fs::is_directory(input)) {
        tempDir = input;
    } else {
        fprintf(stderr, "Error: Input must be an MSW file or directory\n");
        return -1;
    }

    printf("Output: %s\n", outputMsw.string().c_str());

    // Process each FXC file in the directory
    int fxcCount = 0;
    int patchedCount = 0;
    int skippedCount = 0;

    for (const auto& entry : fs::directory_iterator(tempDir)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(tolower(c));
        if (ext != ".fxc") continue;

        fxcCount++;
        std::string fxcPath = entry.path().string();
        std::string fxcName = entry.path().filename().string();

        // Load FXC data
        std::ifstream file(fxcPath, std::ios::binary | std::ios::ate);
        if (!file) {
            fprintf(stderr, "  [%s] Error: Could not open file\n", fxcName.c_str());
            continue;
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> fxcData(fileSize);
        if (!file.read(reinterpret_cast<char*>(fxcData.data()), fileSize)) {
            fprintf(stderr, "  [%s] Error: Could not read file\n", fxcName.c_str());
            continue;
        }
        file.close();

        // ================================================================
        // PHASE 1: Detection (Read-Only)
        // Detect CB layout to determine if this is an S9 shader
        // S9: Camera=CB3, ModelInstance=CB2
        // S7: Camera=CB2, ModelInstance=CB3
        // ================================================================
        dxbc::CBLayoutInfo layoutInfo = dxbc::DetectCBLayout(fxcData.data(), fxcData.size());

        bool wasPatched = false;
        int totalShexPatches = 0;
        int totalRdefPatches = 0;
        int totalSrvPatches = 0;
        int sunDataPatches = 0;
        int uberFlagsPatches = 0;
        int featureFlagBit1Patches = 0;
        int shadowBlendPatches = 0;

        // ================================================================
        // PHASE 2: Content Patches (BEFORE CB Swap!)
        // These patches look for S9's original CB layout:
        //   cb2 = CBufModelInstance (contains lighting.packedSunData at cb2[11].w)
        //   cb3 = CBufCommonPerCamera
        // We MUST patch these patterns BEFORE swapping CB2<->CB3!
        // ================================================================

        if (layoutInfo.needsSwap) {
            printf("  [%s] %s\n", fxcName.c_str(), layoutInfo.reason.c_str());

            // Patch 1: Sun Data Unpacking (CRITICAL - must be before CB swap)
            // S9 shaders unpack packedSunData as integer with bit shifts:
            //   ishr rX.w, cb2[11].w, l(16)   -> extract upper 16 bits (sun visibility)
            //   and rX.w, cb2[11].w, l(0xFFFF) -> extract lower 16 bits (sun intensity)
            //   mul rX.w, rX.w, l(0.00003052)  -> scale to 0-1
            // R5SDK provides cb2[11].w as direct float (skyDirSunVis.w)
            // This patch converts to direct float reads like S7 shaders do
            // NOTE: We search for cb2[11].w because CB swap hasn't happened yet!
            dxbc::PatchResult sunDataResult = dxbc::PatchSunDataUnpacking(fxcData);
            if (sunDataResult.success && sunDataResult.shexPatches > 0) {
                sunDataPatches = sunDataResult.shexPatches;
                wasPatched = true;
            }
        }

        // Patch 2: Uber Feature Flags - Bit 2 (detail texture blending)
        // Patches "and rX.?, cb0[24].?, l(2)" to "mov rX.?, l(0)"
        // Forces simple blending instead of overlay blending (fixes darker materials)
        // cb0 is not affected by CB2<->CB3 swap
        {
            dxbc::PatchResult uberFlagsResult = dxbc::PatchUberFeatureFlags(fxcData);
            if (uberFlagsResult.success && uberFlagsResult.shexPatches > 0) {
                uberFlagsPatches = uberFlagsResult.shexPatches;
                wasPatched = true;
            }
        }

        // Patch 3: Feature Flag Bit 1 (cavity/AO blending mode)
        // Patches "and rX.?, cb0[24].?, l(1)" to "mov rX.?, l(0)"
        // Forces standard blending path
        {
            dxbc::PatchResult bit1Result = dxbc::PatchFeatureFlagBit1(fxcData);
            if (bit1Result.success && bit1Result.shexPatches > 0) {
                featureFlagBit1Patches = bit1Result.shexPatches;
                wasPatched = true;
            }
        }

        // Patch 4: Shadow Blend Multiply (CRITICAL for fixing sun flickering)
        // S9 shaders have: mul r0.w, r0.w, r6.z (shadow_result * shadow_blend)
        // After sun data patch, both r0.w and r6.z contain the same cb3[11].w value
        // This causes sunVis * sunVis = sunVis^2 which:
        //   1. Makes lighting darker (squared attenuation)
        //   2. Amplifies frame-to-frame variations causing visible flickering
        // S7 doesn't have this multiply, so we NOP it out
        // TEMPORARILY DISABLED for testing
        /*if (layoutInfo.needsSwap && sunDataPatches > 0) {
            dxbc::PatchResult shadowBlendResult = dxbc::PatchShadowBlendMultiply(fxcData);
            if (shadowBlendResult.success && shadowBlendResult.shexPatches > 0) {
                shadowBlendPatches = shadowBlendResult.shexPatches;
                wasPatched = true;
            }
        }*/

        // ================================================================
        // PHASE 3: SRV Slot Remapping
        // Slot numbers are independent of CB swap
        // t75 -> t61 (g_modelInst)
        // t63 -> t1 (g_boneWeightsExtra)
        // ================================================================
        {
            dxbc::PatchResult srvResult = dxbc::PatchSRVSlots(fxcData, true);
            if (srvResult.success && srvResult.srvPatches > 0) {
                totalSrvPatches = srvResult.srvPatches;
                wasPatched = true;
            }
        }

        // ================================================================
        // PHASE 3.5: Remove ClusteredLighting_t from CBufCommonPerCamera
        // S11 shaders declare ClusteredLighting_t (32 bytes) but never use it
        // This causes CBufCommonPerCamera to be 784 bytes instead of 752
        // R5SDK provides 752-byte buffer, causing buffer binding mismatch
        // ================================================================
        int clusteredLightingPatches = 0;
        {
            dxbc::PatchResult clusteredResult = dxbc::PatchRemoveClusteredLighting(fxcData);
            if (clusteredResult.success && clusteredResult.rdefPatches > 0) {
                clusteredLightingPatches = clusteredResult.rdefPatches;
                wasPatched = true;
            }
        }

        // ================================================================
        // PHASE 4: CB2<->CB3 Swap (MUST BE LAST!)
        // This swaps ALL cb2 and cb3 references in SHEX bytecode and RDEF
        // After this swap:
        //   cb2 = CBufCommonPerCamera (S7 layout)
        //   cb3 = CBufModelInstance (S7 layout)
        // ================================================================
        if (layoutInfo.needsSwap) {
            dxbc::PatchResult patchResult = dxbc::SwapCB2CB3(fxcData);

            if (patchResult.success) {
                totalShexPatches += patchResult.shexPatches;
                totalRdefPatches += patchResult.rdefPatches;
                wasPatched = true;
            } else {
                fprintf(stderr, "           CB Swap Error: %s\n", patchResult.error.c_str());
            }
        }

        // ================================================================
        // PHASE 5: Finalize and Write
        // ================================================================
        if (wasPatched) {
            // Ensure hash is updated (individual patches update it, but be safe)
            dxbc::UpdateHash(fxcData);

            if (!layoutInfo.needsSwap) {
                printf("  [%s] Patches applied (S7 layout, no CB swap)\n", fxcName.c_str());
            }
            printf("           Patched: %d SHEX, %d RDEF, %d SRV, %d CLT, %d UBR, %d BIT1, %d SUN, %d SHDW\n",
                   totalShexPatches, totalRdefPatches, totalSrvPatches, clusteredLightingPatches,
                   uberFlagsPatches, featureFlagBit1Patches, sunDataPatches, shadowBlendPatches);

            // Write patched FXC back
            std::ofstream outFile(fxcPath, std::ios::binary);
            if (outFile) {
                outFile.write(reinterpret_cast<const char*>(fxcData.data()), fxcData.size());
                outFile.close();
                patchedCount++;
            } else {
                fprintf(stderr, "           Error: Could not write patched file\n");
            }
        } else {
            printf("  [%s] %s (no patch needed)\n", fxcName.c_str(),
                   layoutInfo.needsSwap ? layoutInfo.reason.c_str() : "S7 layout");
            skippedCount++;
        }
    }

    printf("\nFXC Processing Summary:\n");
    printf("  Total: %d, Patched: %d, Skipped: %d\n", fxcCount, patchedCount, skippedCount);

    // Convert data.json to legacy version
    printf("\nConverting to legacy format (shader v12, shaderset v11)...\n");
    convert(tempDir.string().c_str(), SHADER_VERSION_LEGACY, SHADERSET_VERSION_LEGACY);

    // Repack to MSW
    printf("\nRepacking to MSW...\n");
    pack(tempDir.string().c_str());

    // Move output MSW to final location
    fs::path packedMsw = tempDir.string() + ".msw";
    if (fs::exists(packedMsw)) {
        try {
            if (fs::exists(outputMsw)) {
                fs::remove(outputMsw);
            }
            fs::rename(packedMsw, outputMsw);
            printf("Output: %s\n", outputMsw.string().c_str());
        } catch (const fs::filesystem_error& e) {
            fprintf(stderr, "Error moving output: %s\n", e.what());
            // Try copy instead
            try {
                fs::copy_file(packedMsw, outputMsw, fs::copy_options::overwrite_existing);
                fs::remove(packedMsw);
                printf("Output: %s\n", outputMsw.string().c_str());
            } catch (const fs::filesystem_error& e2) {
                fprintf(stderr, "Error copying output: %s\n", e2.what());
            }
        }
    } else {
        fprintf(stderr, "Error: Packed MSW not created\n");
    }

    // Cleanup temp directory if we created it
    if (createdTempDir && fs::exists(tempDir)) {
        try {
            fs::remove_all(tempDir);
            printf("Cleaned up temp directory\n");
        } catch (const fs::filesystem_error& e) {
            fprintf(stderr, "Warning: Could not clean up temp directory: %s\n", e.what());
        }
    }

    printf("\n=== Conversion Complete ===\n");
    return 0;  // Converted successfully
}

// Batch convert all MSW files in a directory to legacy format
void convertLegacyBatch(const char* inputDir, const char* outputDir) {
    printf("=== Batch S9 to Legacy Shader Converter ===\n");
    printf("Input directory: %s\n", inputDir);
    printf("Output directory: %s\n", outputDir);

    // Create output directory if needed
    fs::create_directories(outputDir);

    int totalCount = 0;
    int convertedCount = 0;
    int failedCount = 0;

    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(tolower(c));
        if (ext != ".msw") continue;

        totalCount++;
        std::string inputMsw = entry.path().string();
        std::string outputMsw = (fs::path(outputDir) / entry.path().filename()).string();

        printf("\n[%d] Processing: %s\n", totalCount, entry.path().filename().string().c_str());
        printf("----------------------------------------\n");

        try {
            int result = convertLegacy(inputMsw.c_str(), outputMsw.c_str());
            if (result == 0) {
                convertedCount++;
            } else {
                failedCount++;
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "Error: %s\n", e.what());
            failedCount++;
        }
    }

    printf("\n========================================\n");
    printf("Batch Conversion Complete\n");
    printf("========================================\n");
    printf("Total: %d\n", totalCount);
    printf("  Converted: %d\n", convertedCount);
    printf("  Failed:    %d\n", failedCount);
    printf("Output directory: %s\n", outputDir);
}

void printUsage() {
    printf("MSWUnPacker - MultiShaderWrapper Pack/Unpack/Convert Tool\n\n");
    printf("Usage:\n");
    printf("  MSWUnPacker unpack <msw_file>           - Unpack .msw file to directory\n");
    printf("  MSWUnPacker pack <directory>            - Pack directory to .msw file\n");
    printf("  MSWUnPacker convert <directory> [version] - Convert data.json to target version\n");
    printf("  MSWUnPacker convert-legacy <input> [output] - S9->S3 with auto CB2/CB3 swap\n");
    printf("  MSWUnPacker convert-rsx <json> <outdir> [version] - Convert rex-rsx export to MSW format\n");
    printf("\n");
    printf("Convert versions:\n");
    printf("  legacy  - Shader v12, ShaderSet v11 (r5sdk/S3 compatible)\n");
    printf("  s9      - Shader v12, ShaderSet v12 (S9-S10 compatible)\n");
    printf("  current - Shader v15, ShaderSet v14 (latest)\n");
    printf("  <number> - Specific shader version (shaderset auto-selected)\n");
    printf("\n");
    printf("Shader versions:   v8, v12 (r5sdk), v13, v14, v15 (current), v16\n");
    printf("ShaderSet versions: v8, v11 (r5sdk), v12 (S9), v13, v14 (current)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  MSWUnPacker unpack shader.msw\n");
    printf("  MSWUnPacker pack ./shader_dir\n");
    printf("  MSWUnPacker convert ./shader_dir legacy\n");
    printf("  MSWUnPacker convert ./shader_dir s9       # For S9-S10 target\n");
    printf("  MSWUnPacker convert-rsx 0x123.json ./output legacy\n");
    printf("\n");
    printf("S9 to Legacy Conversion (with auto CB swap):\n");
    printf("  MSWUnPacker convert-legacy shader.msw              # Single file\n");
    printf("  MSWUnPacker convert-legacy shader.msw out.msw      # Single file with output\n");
    printf("  MSWUnPacker convert-legacy ./shader_dir            # Directory\n");
    printf("  MSWUnPacker convert-legacy ./s9_shaders/ ./out/    # Batch directory\n");
    printf("\n");
    printf("The convert-legacy command automatically:\n");
    printf("  1. Detects S9 CB layout (CBufCommonPerCamera at CB3)\n");
    printf("  2. Swaps CB2<->CB3 in SHEX bytecode and RDEF metadata\n");
    printf("  3. Remaps SRV slots:\n");
    printf("     - t75->t61 (g_modelInst)\n");
    printf("     - t63->t1  (g_boneWeightsExtra)\n");
    printf("  4. Patches c_uberFeatureFlags & 2 check (detail texture fix)\n");
    printf("     Forces simple blending instead of overlay blending (fixes darker materials)\n");
    printf("  5. Updates DXBC hash\n");
    printf("  6. Converts version headers to legacy format\n");
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printUsage();
        return 1;
    }

    if (!strncmp(argv[1], "unpack", 7)) {
        if (argc != 3) {
            fprintf(stderr, "Usage: MSWUnPacker unpack <msw_file>\n");
            return 1;
        }
        unpack(argv[2]);
    }
    else if (!strncmp(argv[1], "pack", 5)) {
        if (argc != 3) {
            fprintf(stderr, "Usage: MSWUnPacker pack <directory>\n");
            return 1;
        }
        pack(argv[2]);
    }
    else if (!strncmp(argv[1], "convert", 8)) {
        if (argc < 3) {
            fprintf(stderr, "Usage: MSWUnPacker convert <directory> [legacy|s9|current|<version>]\n");
            return 1;
        }

        int targetShaderVer = SHADER_VERSION_LEGACY;
        int targetShaderSetVer = SHADERSET_VERSION_LEGACY;

        if (argc >= 4) {
            if (!strncmp(argv[3], "legacy", 7)) {
                targetShaderVer = SHADER_VERSION_LEGACY;       // v12
                targetShaderSetVer = SHADERSET_VERSION_LEGACY; // v11
            }
            else if (!strncmp(argv[3], "s9", 3)) {
                targetShaderVer = SHADER_VERSION_LEGACY;       // v12
                targetShaderSetVer = SHADERSET_VERSION_S9;     // v12
            }
            else if (!strncmp(argv[3], "current", 8)) {
                targetShaderVer = SHADER_VERSION_CURRENT;      // v15
                targetShaderSetVer = SHADERSET_VERSION_CURRENT; // v14
            }
            else {
                // Try to parse as number
                targetShaderVer = atoi(argv[3]);
                if (targetShaderVer <= 0) {
                    fprintf(stderr, "Invalid version: %s\n", argv[3]);
                    return 1;
                }
                // Auto-select shaderset version based on shader version
                if (targetShaderVer <= 12) {
                    targetShaderSetVer = 11;
                } else if (targetShaderVer <= 13) {
                    targetShaderSetVer = 12;
                } else {
                    targetShaderSetVer = 14;
                }
            }
        }

        convert(argv[2], targetShaderVer, targetShaderSetVer);
    }
    else if (!strncmp(argv[1], "convert-legacy", 15)) {
        if (argc < 3) {
            fprintf(stderr, "Usage: MSWUnPacker convert-legacy <input.msw|dir> [output.msw|dir]\n");
            return 1;
        }

        const char* inputArg = argv[2];
        const char* outputArg = (argc >= 4) ? argv[3] : nullptr;

        // Check if input is a directory with MSW files (batch mode)
        fs::path input(inputArg);
        if (fs::is_directory(input)) {
            // Check if it contains .msw files (batch mode) or just data files (single dir mode)
            bool hasMswFiles = false;
            bool hasDataJson = fs::exists(input / "data.json");

            for (const auto& entry : fs::directory_iterator(input)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    for (char& c : ext) c = static_cast<char>(tolower(c));
                    if (ext == ".msw") {
                        hasMswFiles = true;
                        break;
                    }
                }
            }

            if (hasMswFiles) {
                // Batch mode: directory contains MSW files
                std::string defaultOutput = std::string(inputArg) + "/converted";
                const char* outputDir = outputArg ? outputArg : defaultOutput.c_str();
                convertLegacyBatch(inputArg, outputDir);
            } else if (hasDataJson) {
                // Single directory mode: already unpacked shader
                convertLegacy(inputArg, outputArg);
            } else {
                fprintf(stderr, "Error: Directory does not contain MSW files or data.json\n");
                return 1;
            }
        } else {
            // Single file mode
            convertLegacy(inputArg, outputArg);
        }
    }
    else if (!strncmp(argv[1], "convert-rsx", 12)) {
        if (argc < 4) {
            fprintf(stderr, "Usage: MSWUnPacker convert-rsx <json_file> <output_dir> [legacy|current|<version>]\n");
            return 1;
        }

        int targetShaderVer = SHADER_VERSION_LEGACY; // Default to legacy (v12)

        if (argc >= 5) {
            if (!strncmp(argv[4], "legacy", 7)) {
                targetShaderVer = SHADER_VERSION_LEGACY;      // v12
            }
            else if (!strncmp(argv[4], "current", 8)) {
                targetShaderVer = SHADER_VERSION_CURRENT;      // v15
            }
            else {
                targetShaderVer = atoi(argv[4]);
                if (targetShaderVer <= 0) {
                    fprintf(stderr, "Invalid version: %s\n", argv[4]);
                    return 1;
                }
            }
        }

        convertRsx(argv[2], argv[3], targetShaderVer);
    }
    else if (!strncmp(argv[1], "help", 5) || !strncmp(argv[1], "-h", 3) || !strncmp(argv[1], "--help", 7)) {
        printUsage();
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        printUsage();
        return 1;
    }

    return 0;
}





// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
