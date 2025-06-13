#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <cstring>
extern "C" {
#include "cJSON.h"
}

std::map<std::string, cJSON*> inputMap;
std::map<std::string, cJSON*> schemaMap;
cJSON* schemaRoot = nullptr;

// Read file into string
std::string readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Helper to resolve $ref in schema, including /items/N
cJSON* resolveRef(cJSON* root, const std::string& ref) {
    if (ref.rfind("#/definitions/", 0) == 0) {
        std::string path = ref.substr(strlen("#/definitions/"));
        size_t slash = path.find('/');
        std::string defName = path.substr(0, slash);
        cJSON* node = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "definitions"), defName.c_str());
        while (slash != std::string::npos && node) {
            path = path.substr(slash + 1);
            slash = path.find('/');
            std::string key = path.substr(0, slash);
            if (key == "items") {
                node = cJSON_GetObjectItem(node, "items");
            } else {
                int idx = std::atoi(key.c_str());
                node = cJSON_GetArrayItem(node, idx);
            }
        }
        return node;
    }
    return nullptr;
}

// Helper to check if a value is in an enum array
bool isInEnum(cJSON* enumNode, const std::string& value) {
    if (!enumNode || !cJSON_IsArray(enumNode)) return false;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, enumNode) {
        if (cJSON_IsString(item) && value == item->valuestring) return true;
    }
    return false;
}

// Validate a value against a schema node
bool validateValue(cJSON* value, cJSON* schema, cJSON* schemaRoot) {
    if (!schema) return false;

    // Handle $ref
    cJSON* refNode = cJSON_GetObjectItem(schema, "$ref");
    if (refNode && cJSON_IsString(refNode)) {
        cJSON* resolved = resolveRef(schemaRoot, refNode->valuestring);
        if (!resolved) return false;
        return validateValue(value, resolved, schemaRoot);
    }

    // Handle type
    cJSON* typeNode = cJSON_GetObjectItem(schema, "type");
    if (typeNode && cJSON_IsString(typeNode)) {
        std::string type = typeNode->valuestring;
        if (type == "string") {
            if (!cJSON_IsString(value)) return false;
            cJSON* enumNode = cJSON_GetObjectItem(schema, "enum");
            if (enumNode && !isInEnum(enumNode, value->valuestring)) return false;
            return true;
        }
        if (type == "number" || type == "integer") {
            if (!cJSON_IsNumber(value)) return false;
            cJSON* minNode = cJSON_GetObjectItem(schema, "minimum");
            cJSON* maxNode = cJSON_GetObjectItem(schema, "maximum");
            if (minNode && value->valuedouble < minNode->valuedouble) return false;
            if (maxNode && value->valuedouble > maxNode->valuedouble) return false;
            return true;
        }
        if (type == "array") {
            if (!cJSON_IsArray(value)) return false;
            cJSON* itemsNode = cJSON_GetObjectItem(schema, "items");
            if (!itemsNode) return false;
            // If items is an array, validate each item by index
            if (cJSON_IsArray(itemsNode)) {
                int arrSize = cJSON_GetArraySize(itemsNode);
                for (int i = 0; i < cJSON_GetArraySize(value); ++i) {
                    cJSON* v = cJSON_GetArrayItem(value, i);
                    cJSON* s = cJSON_GetArrayItem(itemsNode, i % arrSize);
                    if (!validateValue(v, s, schemaRoot)) return false;
                }
                return true;
            } else {
                // items is a schema, validate each element
                cJSON* arrItem = nullptr;
                cJSON_ArrayForEach(arrItem, value) {
                    if (!validateValue(arrItem, itemsNode, schemaRoot)) return false;
                }
                return true;
            }
        }
        if (type == "object") {
            // Not handled in this minimal example
            return false;
        }
    }
    // Handle enum at root
    cJSON* enumNode = cJSON_GetObjectItem(schema, "enum");
    if (enumNode && cJSON_IsString(value) && !isInEnum(enumNode, value->valuestring)) return false;

    return true;
}

// Validate function: takes key, uses maps
bool validate(const std::string& key) {
    auto itInput = inputMap.find(key);
    auto itSchema = schemaMap.find(key);
    if (itInput == inputMap.end() || itSchema == schemaMap.end()) {
        std::cout << key << " : NOT FOUND" << std::endl;
        return false;
    }
    bool valid = validateValue(itInput->second, itSchema->second, schemaRoot);
    std::cout << key << " : " << (valid ? "PASS" : "FAIL") << std::endl;
    return valid;
}

int main() {
    std::string inputStr = readFile("input.json");
    std::string schemaStr = readFile("schema.json");
    if (inputStr.empty() || schemaStr.empty()) return 1;

    cJSON* inputRoot = cJSON_Parse(inputStr.c_str());
    schemaRoot = cJSON_Parse(schemaStr.c_str());
    if (!inputRoot || !schemaRoot) {
        std::cerr << "Error parsing JSON files." << std::endl;
        return 1;
    }

    // Store all input data in memory
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, inputRoot) {
        inputMap[item->string] = item;
    }

    // Store all schema properties in memory
    cJSON* properties = cJSON_GetObjectItem(schemaRoot, "properties");
    if (properties) {
        cJSON* prop = nullptr;
        cJSON_ArrayForEach(prop, properties) {
            schemaMap[prop->string] = prop;
        }
    }

    // Example usage: validate any key
    validate("picture/aspectratio");
    validate("picture/picturemode");
    validate("picture/dolbyvisionmode");
    validate("sound/bassboost"); // Should print NOT FOUND or FAIL if not in schema
    validate("sound/enhancespeech");
    validate("picture/lowlatencystate");
    validate("picture/resolutio");
    // You can call validate(key) for any key as needed

    cJSON_Delete(inputRoot);
    cJSON_Delete(schemaRoot);
    return 0;
}