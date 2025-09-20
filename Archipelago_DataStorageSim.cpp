#include <cstdint>
#include <functional>
#include <json/value.h>
#include <json/writer.h>
#include <map>
#include <string>

extern Json::Value sp_save_root;
extern std::string sp_save_path;

extern Json::FastWriter writer;

extern const int AP_OFFLINE_SLOT;

void WriteFileJSON(Json::Value root, std::string path);
bool parse_response(std::string msg, std::string &request);

void operation_replace(std::string key, Json::Value new_val) {
    Json::Value store = sp_save_root["store"];
    store[key] = new_val;
}
void operation_or(std::string key, Json::Value new_val) {
    // Semi-broken implementation. Assumes integer or bool values atm
    Json::Value store = sp_save_root["store"];
    if (store[key].isInt64()) {
        int64_t value = store[key].asInt64();
        store[key] = value | new_val.asInt64();
    } else if (store[key].isBool()) {
        bool value = store[key].asBool();
        store[key] = value || new_val.asBool();
    }

}
void operation_default(std::string key, Json::Value new_val) {
    // Noop as default is set in resolveDataStorageOp.
}

static std::map<const std::string , std::function<void(std::string,Json::Value)>> known_operations = {
    {"replace", operation_replace},
    {"or", operation_or},
    {"default", operation_default},
};

void resolveDataStorageOp(Json::Value dataOp) {
    Json::Value store = sp_save_root["store"];
    if (dataOp["cmd"] == "Set" || dataOp["cmd"] == "SetNotify") {
        std::string key = dataOp["key"].asString();
        Json::Value original_value = store.get(key, Json::nullValue);
        if (!store.get(key, Json::nullValue) && dataOp.get("default", Json::nullValue)) {
            // Key not in store. Set default
            store[key] = dataOp["default"];
        }
        for (Json::Value atomicOp : dataOp["operations"]) {
            std::string operation_str = atomicOp["operation"].asString();
            Json::Value newVal = atomicOp["value"];
            known_operations[operation_str](key, newVal);
        }
        WriteFileJSON(sp_save_root, sp_save_path);
        if (dataOp["want_reply"].asBool()) {
            Json::Value fake_msg;
            std::string req;
            fake_msg["cmd"] = "SetReply";
            fake_msg["key"] = key;
            fake_msg["value"] = store[key];
            fake_msg["original_value"] = original_value;
            fake_msg["slot"] = AP_OFFLINE_SLOT;
            parse_response(writer.write(fake_msg), req);
        }
    } else if (dataOp["cmd"] == "Get") {
        Json::Value fake_msg;
        std::string req;
        fake_msg["cmd"] = "Retrieved";
        fake_msg["keys"] = Json::objectValue;
        for (Json::Value key_v : dataOp["keys"]) {
            std::string key = key_v.asString();
            fake_msg["keys"][key] = sp_save_root.get(key, Json::nullValue);
        }
        parse_response(writer.write(fake_msg), req);
    }
}
