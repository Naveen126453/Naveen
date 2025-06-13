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

std::string readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

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

bool isInEnum(cJSON* enumNode, const std::string& value) {
    if (!enumNode || !cJSON_IsArray(enumNode)) return false;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, enumNode) {
        if (cJSON_IsString(item) && value == item->valuestring) return true;
    }
    return false;
}

bool validateValue(cJSON* value, cJSON* schema, cJSON* root, std::string path = "") {
    if (!schema) {
        std::cerr << "❌ Schema missing at " << path << "\n";
        return false;
    }

    cJSON* refNode = cJSON_GetObjectItem(schema, "$ref");
    if (refNode && cJSON_IsString(refNode)) {
        cJSON* resolved = resolveRef(root, refNode->valuestring);
        if (resolved) return validateValue(value, resolved, root, path + " → $ref");
        std::cerr << "❌ Invalid $ref: " << refNode->valuestring << " at " << path << "\n";
        return false;
    }

    cJSON* type = cJSON_GetObjectItem(schema, "type");
    if (type && cJSON_IsString(type)) {
        std::string t = type->valuestring;
        if (t == "string" && !cJSON_IsString(value)) {
            std::cerr << "❌ Expected string at " << path << "\n";
            return false;
        }
        if (t == "number" && !cJSON_IsNumber(value)) {
            std::cerr << "❌ Expected number at " << path << "\n";
            return false;
        }
        if (t == "array" && !cJSON_IsArray(value)) {
            std::cerr << "❌ Expected array at " << path << "\n";
            return false;
        }
        if (t == "object" && !cJSON_IsObject(value)) {
            std::cerr << "❌ Expected object at " << path << "\n";
            return false;
        }
    }

    cJSON* enumNode = cJSON_GetObjectItem(schema, "enum");
    if (enumNode && cJSON_IsArray(enumNode)) {
        if (cJSON_IsString(value) && !isInEnum(enumNode, value->valuestring)) {
            std::cerr << "❌ Value \"" << value->valuestring << "\" not in enum at " << path << "\n";
            return false;
        }
    }

    if (cJSON_IsNumber(value)) {
        cJSON* min = cJSON_GetObjectItem(schema, "minimum");
        cJSON* max = cJSON_GetObjectItem(schema, "maximum");
        cJSON* mult = cJSON_GetObjectItem(schema, "multipleOf");

        if (min && value->valuedouble < min->valuedouble) {
            std::cerr << "❌ Number " << value->valuedouble << " < min " << min->valuedouble << " at " << path << "\n";
            return false;
        }
        if (max && value->valuedouble > max->valuedouble) {
            std::cerr << "❌ Number " << value->valuedouble << " > max " << max->valuedouble << " at " << path << "\n";
            return false;
        }
        if (mult && fmod(value->valuedouble, mult->valuedouble) != 0.0) {
            std::cerr << "❌ Number " << value->valuedouble << " not multiple of " << mult->valuedouble << " at " << path << "\n";
            return false;
        }
    }

    if (cJSON_IsArray(value)) {
        cJSON* itemsSchema = cJSON_GetObjectItem(schema, "items");
        if (!itemsSchema) {
            std::cerr << "❌ Array with no 'items' schema at " << path << "\n";
            return false;
        }

        int index = 0;
        cJSON* element = nullptr;
        cJSON_ArrayForEach(element, value) {
            cJSON* currentSchema = nullptr;

            if (cJSON_IsArray(itemsSchema)) {
                currentSchema = cJSON_GetArrayItem(itemsSchema, index);
                if (!currentSchema) currentSchema = cJSON_GetArrayItem(itemsSchema, 0);
            } else if (cJSON_IsObject(itemsSchema)) {
                currentSchema = itemsSchema;
            }

            if (!validateValue(element, currentSchema, root, path + "[" + std::to_string(index) + "]"))
                return false;
            ++index;
        }
    }

    return true;
}

void parseInput(const std::string& inputJson) {
    cJSON* root = cJSON_Parse(inputJson.c_str());
    if (!root) {
        std::cerr << "Failed to parse input\n";
        return;
    }
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        inputMap[item->string] = item;
    }
}

void parseSchema(const std::string& schemaJson) {
    schemaRoot = cJSON_Parse(schemaJson.c_str());
    if (!schemaRoot) {
        std::cerr << "Failed to parse schema\n";
        return;
    }
    cJSON* properties = cJSON_GetObjectItem(schemaRoot, "properties");
    if (!properties) return;
    cJSON* prop = nullptr;
    cJSON_ArrayForEach(prop, properties) {
        if (cJSON_IsObject(prop)) schemaMap[prop->string] = prop;
    }
}

void validateInputAgainstSchema() {
    for (const auto& pair : inputMap) {
        const std::string& key = pair.first;
        cJSON* value = pair.second;
        cJSON* schema = schemaMap.count(key) ? schemaMap[key] : nullptr;

        std::cout << "[*] Validating key: " << key << " ... ";
        if (!schema) {
            std::cout << "⚠️ No schema found\n";
            continue;
        }

        if (!validateValue(value, schema, schemaRoot, key)) {
            std::cout << "❌ Invalid\n";
        } else {
            std::cout << "✅ Valid\n";
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.json> <schema.json>\n";
        return 1;
    }

    std::string inputStr = readFile(argv[1]);
    std::string schemaStr = readFile(argv[2]);

    parseInput(inputStr);
    parseSchema(schemaStr);
    validateInputAgainstSchema();

    cJSON_Delete(schemaRoot);
    return 0;
}

