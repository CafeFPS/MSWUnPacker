// MSWUnPacker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <fstream>
#include "multishader.h"

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/document.h"

void unpack(const char* path) {
    CMultiShaderWrapperIO::ShaderCache_t shaderCache = {};
    MSW_ParseFile(path,shaderCache);
    switch (shaderCache.type) {
    case MultiShaderWrapperFileType_e::SHADER:
    {
        fs::create_directories(shaderCache.shader->name);
        for (size_t i = 0; i < shaderCache.shader->entries.size(); i++) {
            
            auto& entry = shaderCache.shader->entries[i];
            if(!entry.buffer)continue;
            std::ofstream out{ std::format("{}/{}.fxc",shaderCache.shader->name,i).c_str(),std::ios::binary };
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
        std::ofstream jsonOut{ std::format("{}/data.json",shaderCache.shader->name).c_str() };
        jsonOut.write(jsonBuf.GetString(), jsonBuf.GetSize());
        jsonOut.close();
    }
        break;
    case MultiShaderWrapperFileType_e::SHADERSET:
    {
        rapidjson::StringBuffer jsonBuf{};
        rapidjson::PrettyWriter<rapidjson::StringBuffer> jsonWriter{};
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
        std::ofstream jsonOut{ std::format("{}/data.json",shaderCache.shader->name).c_str() };
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

int main(int argc, char** argv)
{
    if (argc != 3) {
        printf("wrong parameter count usage: \"MSWUnPacker [unpack|pack] [path]\"\n");
        return 1;
    }
    if (!strncmp(argv[1], "unpack", 7)) {
        unpack(argv[2]);
    }
    else if (!strncmp(argv[1], "pack", 5)) {
        pack(argv[2]);
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
