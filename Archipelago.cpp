#include "Archipelago.h"

#include "ixwebsocket/IXNetSystem.h"
#include "ixwebsocket/IXWebSocket.h"
#include "ixwebsocket/IXUserAgent.h"

#include <cstdint>
#include <random>
#include <fstream>
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#include <deque>
#include <string>
#include <chrono>
#include <utility>
#include <vector>

#define AP_OFFLINE_SLOT 1404
#define AP_OFFLINE_NAME "You"

//Setup Stuff
bool init = false;
bool auth = false;
bool refused = false;
bool multiworld = true;
bool isSSL = true;
bool ssl_success = false;
int ap_player_id;
std::string ap_player_name;
std::string ap_ip;
std::string ap_game;
std::string ap_passwd;
std::uint64_t ap_uuid = 0;
std::mt19937 rando;
AP_NetworkVersion client_version = {0,2,6}; // Default for compatibility reasons

//Deathlink Stuff
bool deathlinkstat = false;
bool deathlinksupported = false;
bool enable_deathlink = false;
int deathlink_amnesty = 0;
int cur_deathlink_amnesty = 0;

// Message System
std::deque<AP_Message*> messageQueue;
bool queueitemrecvmsg = true;

// Data Maps
std::map<int, std::string> map_player_id_alias;
std::map<int64_t, std::string> map_location_id_name;
std::map<int64_t, std::string> map_item_id_name;

// Callback function pointers
void (*resetItemValues)();
void (*getitemfunc)(int64_t,bool);
void (*checklocfunc)(int64_t);
void (*recvdeath)() = nullptr;
void (*setreplyfunc)(AP_SetReply) = nullptr;

// Serverdata Management
std::map<std::string,AP_DataType> map_serverdata_typemanage;
AP_GetServerDataRequest resync_serverdata_request;
int last_item_idx = 0;

// Singleplayer Seed Info
std::ofstream sp_save_file;
Json::Value sp_save_root;

//Misc Data for Clients
AP_RoomInfo lib_room_info;

//Server Data Stuff
std::map<std::string, AP_GetServerDataRequest*> map_server_data;
std::chrono::steady_clock::time_point last_send_req = std::chrono::steady_clock::now();

//Slot Data Stuff
std::map<std::string, void (*)(int)> map_slotdata_callback_int;
std::map<std::string, void (*)(std::string)> map_slotdata_callback_raw;
std::map<std::string, void (*)(std::map<int,int>)> map_slotdata_callback_mapintint;
std::vector<std::string> slotdata_strings;

ix::WebSocket webSocket;
Json::Reader reader;
Json::FastWriter writer;

Json::Value sp_ap_root;

// PRIV Func Declarations Start
bool parse_response(std::string msg, std::string &request);
void APSend(std::string req);
void WriteSPSave();
std::string getItemName(int64_t id);
std::string getLocationName(int64_t id);
// PRIV Func Declarations End

void AP_Init(const char* ip, const char* game, const char* player_name, const char* passwd) {
    multiworld = true;
    
    auto milliseconds_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(last_send_req.time_since_epoch()).count();
    rando = std::mt19937(milliseconds_since_epoch);

    if (!strcmp(ip,"")) {
        ip = "archipelago.gg:38281";
        printf("AP: Using default Server Adress: '%s'\n", ip);
    } else {
        printf("AP: Using Server Adress: '%s'\n", ip);
    }
    ap_ip = std::string(ip);
    ap_game = std::string(game);
    ap_player_name = std::string(player_name);
    ap_passwd = std::string(passwd);

    printf("AP: Initializing...\n");

    //Connect to server
    ix::initNetSystem();
    webSocket.setUrl("wss://" + ap_ip);
    webSocket.setOnMessageCallback([](const ix::WebSocketMessagePtr& msg)
        {
            if (msg->type == ix::WebSocketMessageType::Message)
            {
                std::string request;
                if (parse_response(msg->str, request)) {
                    APSend(request);
                }
            }
            else if (msg->type == ix::WebSocketMessageType::Open)
            {
                printf("AP: Connected to Archipelago\n");
            }
            else if (msg->type == ix::WebSocketMessageType::Error || msg->type == ix::WebSocketMessageType::Close)
            {
                auth = false;
                for (std::pair<std::string,AP_GetServerDataRequest*> itr : map_server_data) {
                    itr.second->status = AP_RequestStatus::Error;
                    map_server_data.erase(itr.first);
                }
                printf("AP: Error connecting to Archipelago. Retries: %d\n", msg->errorInfo.retries-1);
                if (msg->errorInfo.retries-1 >= 2 && isSSL && !ssl_success) {
                    printf("AP: SSL connection failed. Attempting unencrypted...\n");
                    webSocket.setUrl("ws://" + ap_ip);
                    isSSL = false;
                }
            }
        }
    );
    webSocket.setPingInterval(45);

    map_player_id_alias[0] = "Archipelago";
}

void AP_Init(const char* filename) {
    multiworld = false;
    std::ifstream mwfile(filename);
    reader.parse(mwfile,sp_ap_root);
    mwfile.close();
    std::ifstream savefile(std::string(filename) + ".save");
    reader.parse(savefile, sp_save_root);
    savefile.close();
    sp_save_file.open((std::string(filename) + ".save").c_str());
    WriteSPSave();
    ap_player_name = AP_OFFLINE_NAME;
}

void AP_Start() {
    init = true;
    if (multiworld) {
        webSocket.start();
    } else {
        if (!sp_save_root.get("init", false).asBool()) {
            sp_save_root["init"] = true;
            sp_save_root["checked_locations"] = Json::arrayValue;
            sp_save_root["store"] = Json::objectValue;
        }
        Json::Value fake_msg;
        fake_msg[0]["cmd"] = "Connected";
        fake_msg[0]["slot"] = AP_OFFLINE_SLOT;
        fake_msg[0]["players"] = Json::arrayValue;
        fake_msg[0]["players"][0]["team"] = 0;
        fake_msg[0]["players"][0]["slot"] = AP_OFFLINE_SLOT;
        fake_msg[0]["players"][0]["alias"] = AP_OFFLINE_NAME;
        fake_msg[0]["players"][0]["name"] = AP_OFFLINE_NAME;
        fake_msg[0]["checked_locations"] = sp_save_root["checked_locations"];
        fake_msg[0]["slot_data"] = sp_ap_root["slot_data"];
        std::string req;
        parse_response(writer.write(fake_msg), req);
        fake_msg.clear();
        fake_msg[0]["cmd"] = "DataPackage";
        fake_msg[0]["data"] = sp_ap_root["data_package"]["data"];
        parse_response(writer.write(fake_msg), req);
        fake_msg.clear();
        fake_msg[0]["cmd"] = "ReceivedItems";
        fake_msg[0]["index"] = 0;
        fake_msg[0]["items"] = Json::arrayValue;
        for (int i = 0; i < sp_save_root["checked_locations"].size(); i++) {
            Json::Value item;
            item["item"] = sp_ap_root["location_to_item"][sp_save_root["checked_locations"][i].asString()].asInt64();
            item["location"] = 0;
            item["player"] = ap_player_id;
            fake_msg[0]["items"].append(item);
        }
        parse_response(writer.write(fake_msg), req);
    }
}

bool AP_IsInit() {
    return init;
}

void AP_SetClientVersion(AP_NetworkVersion* version) {
    client_version.major = version->major;
    client_version.minor = version->minor;
    client_version.build = version->build;
}

void AP_SendItem(int64_t idx) {
    printf(("AP: Checked " + getLocationName(idx) + ". Informing Archipelago...\n").c_str());
    if (multiworld) {
        Json::Value req_t;
        req_t[0]["cmd"] = "LocationChecks";
        req_t[0]["locations"][0] = idx;
        APSend(writer.write(req_t));
    } else {
        for (auto itr : sp_save_root["checked_locations"]) {
            if (itr.asInt64() == idx) {
                return;
            }
        }
        int64_t recv_item_id = sp_ap_root["location_to_item"].get(std::to_string(idx), 0).asInt64();
        if (recv_item_id == 0) return;
        Json::Value fake_msg;
        fake_msg[0]["cmd"] = "ReceivedItems";
        fake_msg[0]["index"] = last_item_idx+1;
        fake_msg[0]["items"][0]["item"] = recv_item_id;
        fake_msg[0]["items"][0]["location"] = idx;
        fake_msg[0]["items"][0]["player"] = ap_player_id;
        std::string req;
        parse_response(writer.write(fake_msg), req);
        sp_save_root["checked_locations"].append(idx);
        WriteSPSave();
        fake_msg.clear();
        fake_msg[0]["cmd"] = "RoomUpdate";
        fake_msg[0]["checked_locations"][0] = idx;
        parse_response(writer.write(fake_msg), req);
    }
}

void AP_StoryComplete() {
    if (!multiworld) return;
    Json::Value req_t;
    req_t[0]["cmd"] = "StatusUpdate";
    req_t[0]["status"] = 30; //CLIENT_GOAL
    APSend(writer.write(req_t));
}

void AP_DeathLinkSend() {
    if (!enable_deathlink || !multiworld) return;
    if (cur_deathlink_amnesty > 0) {
        cur_deathlink_amnesty--;
        return;
    }
    cur_deathlink_amnesty = deathlink_amnesty;
    std::chrono::time_point<std::chrono::system_clock> timestamp = std::chrono::system_clock::now();
    Json::Value req_t;
    req_t[0]["cmd"] = "Bounce";
    req_t[0]["data"]["time"] = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
    req_t[0]["data"]["source"] = ap_player_name; // Name and Shame >:D
    req_t[0]["tags"][0] = "DeathLink";
    APSend(writer.write(req_t));
}

void AP_EnableQueueItemRecvMsgs(bool b) {
    queueitemrecvmsg = b;
}

void AP_SetItemClearCallback(void (*f_itemclr)()) {
    resetItemValues = f_itemclr;
}

void AP_SetItemRecvCallback(void (*f_itemrecv)(int64_t,bool)) {
    getitemfunc = f_itemrecv;
}

void AP_SetLocationCheckedCallback(void (*f_locrecv)(int64_t)) {
    checklocfunc = f_locrecv;
}

void AP_SetDeathLinkRecvCallback(void (*f_deathrecv)()) {
    recvdeath = f_deathrecv;
}

void AP_RegisterSlotDataIntCallback(std::string key, void (*f_slotdata)(int)) {
    map_slotdata_callback_int[key] = f_slotdata;
    slotdata_strings.push_back(key);
}

void AP_RegisterSlotDataRawCallback(std::string key, void (*f_slotdata)(std::string)) {
    map_slotdata_callback_raw[key] = f_slotdata;
    slotdata_strings.push_back(key);
}

void AP_RegisterSlotDataMapIntIntCallback(std::string key, void (*f_slotdata)(std::map<int,int>)) {
    map_slotdata_callback_mapintint[key] = f_slotdata;
    slotdata_strings.push_back(key);
}

void AP_SetDeathLinkSupported(bool supdeathlink) {
    deathlinksupported = supdeathlink;
}

bool AP_DeathLinkPending() {
    return deathlinkstat;
}

void AP_DeathLinkClear() {
    deathlinkstat = false;
}

bool AP_IsMessagePending() {
    return !messageQueue.empty();
}

AP_Message* AP_GetLatestMessage() {
    return messageQueue.front();
}

void AP_ClearLatestMessage() {
    if (AP_IsMessagePending()) {
        delete messageQueue.front();
        messageQueue.pop_front();
    }
}

int AP_GetRoomInfo(AP_RoomInfo* client_roominfo) {
    if (!auth) return 1;
    *client_roominfo = lib_room_info;
    return 0;
}

AP_ConnectionStatus AP_GetConnectionStatus() {
    if (refused) {
        return AP_ConnectionStatus::ConnectionRefused;
    }
    if (webSocket.getReadyState() == ix::ReadyState::Open) {
        if (auth) {
            return AP_ConnectionStatus::Authenticated;
        } else {
            return AP_ConnectionStatus::Connected;
        }
    }
    return AP_ConnectionStatus::Disconnected;
}

int AP_GetUUID() {
    return ap_uuid;
}

void AP_SetServerData(AP_SetServerDataRequest* request) {
    // Rate Limiting
    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_send_req).count() < 20) {
        request->status = AP_RequestStatus::Error;
        return;
    }
    last_send_req = std::chrono::steady_clock::now();

    request->status = AP_RequestStatus::Pending;

    Json::Value req_t;
    req_t[0]["cmd"] = "Set";
    req_t[0]["key"] = request->key;
    switch (request->type) {
        case AP_DataType::Int:
            for (int i = 0; i < request->operations.size(); i++) {
                req_t[0]["operations"][i]["operation"] = request->operations[i].operation;
                req_t[0]["operations"][i]["value"] = *((int*)request->operations[i].value);
            }
            break;
        case AP_DataType::Double:
            for (int i = 0; i < request->operations.size(); i++) {
                req_t[0]["operations"][i]["operation"] = request->operations[i].operation;
                req_t[0]["operations"][i]["value"] = *((double*)request->operations[i].value);
            }
            break;
        default:
            for (int i = 0; i < request->operations.size(); i++) {
                req_t[0]["operations"][i]["operation"] = request->operations[i].operation;
                Json::Value data;
                reader.parse((*(std::string*)request->operations[i].value), data);
                req_t[0]["operations"][i]["value"] = data;
            }
            Json::Value default_val_json;
            reader.parse(*((std::string*)request->default_value), default_val_json);
            req_t[0]["default"] = default_val_json;
            break;
    }
    req_t[0]["want_reply"] = request->want_reply;
    map_serverdata_typemanage[request->key] = request->type;
    APSend(writer.write(req_t));
    request->status = AP_RequestStatus::Done;
}

void AP_RegisterSetReplyCallback(void (*f_setreply)(AP_SetReply)) {
    setreplyfunc = f_setreply;
}

void AP_SetNotify(std::map<std::string,AP_DataType> keylist) {
    Json::Value req_t;
    req_t[0]["cmd"] = "SetNotify";
    int i = 0;
    for (std::pair<std::string,AP_DataType> keytypepair : keylist) {
        req_t[0]["keys"][i] = keytypepair.first;
        map_serverdata_typemanage[keytypepair.first] = keytypepair.second;
        i++;
    }
    APSend(writer.write(req_t));
}

void AP_SetNotify(std::string key, AP_DataType type) {
    std::map<std::string,AP_DataType> keylist;
    keylist[key] = type;
    AP_SetNotify(keylist);
}

void AP_GetServerData(AP_GetServerDataRequest* request) {
    request->status = AP_RequestStatus::Pending;

    if (map_server_data.find(request->key) != map_server_data.end()) return;

    map_server_data[request->key] = request;

    Json::Value req_t;
    req_t[0]["cmd"] = "Get";
    req_t[0]["keys"][0] = request->key;
    APSend(writer.write(req_t));
}

// PRIV

bool parse_response(std::string msg, std::string &request) {
    Json::Value root;
    reader.parse(msg, root);
    for (unsigned int i = 0; i < root.size(); i++) {
        const char* cmd = root[i]["cmd"].asCString();
        if (!strcmp(cmd,"RoomInfo")) {
            lib_room_info.version.major = root[i]["version"]["major"].asInt();
            lib_room_info.version.minor = root[i]["version"]["minor"].asInt();
            lib_room_info.version.build = root[i]["version"]["build"].asInt();
            std::vector<std::string> serv_tags;
            for (auto itr : root[i]["tags"]) {
                serv_tags.push_back(itr.asString());
            }
            lib_room_info.tags = serv_tags;
            lib_room_info.password_required = root[i]["password"].asBool();
            std::map<std::string,int> serv_permissions;
            for (auto itr : root[i]["permissions"].getMemberNames()) {
                serv_permissions[itr] = root[i]["permissions"][itr].asInt();
            }
            lib_room_info.permissions = serv_permissions;
            lib_room_info.hint_cost = root[i]["hint_cost"].asInt();
            lib_room_info.location_check_points = root[i]["location_check_points"].asInt();
            lib_room_info.datapackage_version = root[i]["datapackage_version"].asInt();
            std::map<std::string,int> serv_datapkg_versions;
            for (auto itr : root[i]["datapackage_versions"].getMemberNames()) {
                serv_datapkg_versions[itr] = root[i]["datapackage_versions"][itr].asInt();
            }
            lib_room_info.datapackage_versions = serv_datapkg_versions;
            lib_room_info.seed_name = root[i]["seed_name"].asString();
            lib_room_info.time = root[i]["time"].asFloat();

            if (!auth) {
                Json::Value req_t;
                ap_uuid = rando();
                req_t[0]["cmd"] = "Connect";
                req_t[0]["game"] = ap_game;
                req_t[0]["name"] = ap_player_name;
                req_t[0]["password"] = ap_passwd;
                req_t[0]["uuid"] = ap_uuid;
                req_t[0]["tags"] = Json::arrayValue;
                req_t[0]["version"]["major"] = client_version.major;
                req_t[0]["version"]["minor"] = client_version.minor;
                req_t[0]["version"]["build"] = client_version.build;
                req_t[0]["version"]["class"] = "Version";
                req_t[0]["items_handling"] = 7; // Full Remote
                request = writer.write(req_t);
                return true;
            }
        } else if (!strcmp(cmd,"Connected")) {
            // Avoid inconsistency if we disconnected before
            (*resetItemValues)();

            printf("AP: Authenticated\n");
            ap_player_id = root[i]["slot"].asInt();
            for (unsigned int j = 0; j < root[i]["checked_locations"].size(); j++) {
                //Sync checks with server
                int64_t loc_id = root[i]["checked_locations"][j].asInt64();
                (*checklocfunc)(loc_id);
            }
            for (unsigned int j = 0; j < root[i]["players"].size(); j++) {
                map_player_id_alias.insert(std::pair<int,std::string>(root[i]["players"][j]["slot"].asInt(),root[i]["players"][j]["alias"].asString()));
            }
            if (root[i]["slot_data"].get("DeathLink", false).asBool() && deathlinksupported) enable_deathlink = true;
            deathlink_amnesty = root[i]["slot_data"].get("DeathLink_Amnesty", 0).asInt();
            cur_deathlink_amnesty = deathlink_amnesty;
            for (std::string key : slotdata_strings) {
                if (map_slotdata_callback_int.count(key)) {
                    (*map_slotdata_callback_int.at(key))(root[i]["slot_data"][key].asInt());
                    } else if (map_slotdata_callback_raw.count(key)) {
                    (*map_slotdata_callback_raw.at(key))(root[i]["slot_data"][key].asString());
                } else if (map_slotdata_callback_mapintint.count(key)) {
                    std::map<int,int> out;
                    for (auto itr : root[i]["slot_data"][key].getMemberNames()) {
                        out[std::stoi(itr)] = root[i]["slot_data"][key][itr.c_str()].asInt();
                    }
                    (*map_slotdata_callback_mapintint.at(key))(out);
                }
                
            }

            resync_serverdata_request.key = "APCppLastRecv" + ap_player_name + std::to_string(ap_player_id);
            resync_serverdata_request.value = &last_item_idx;
            resync_serverdata_request.type = AP_DataType::Int;
            AP_GetServerData(&resync_serverdata_request);

            Json::Value req_t;
            req_t[0]["cmd"] = "GetDataPackage";
            if (enable_deathlink && deathlinksupported) {
                req_t[1]["cmd"] = "ConnectUpdate";
                req_t[1]["tags"][0] = "DeathLink";
            }
            request = writer.write(req_t);
            return true;
        } else if (!strcmp(cmd,"DataPackage")) {
            for (auto itr : root[i]["data"]["games"]) {
                for (auto itr2 : itr["item_name_to_id"].getMemberNames()) {
                    map_item_id_name[itr["item_name_to_id"][itr2].asInt64()] = itr2;
                }
                for (auto itr2 : itr["location_name_to_id"].getMemberNames()) {
                    map_location_id_name[itr["location_name_to_id"][itr2].asInt64()] = itr2;
                }
            }
            Json::Value req_t;
            req_t[0]["cmd"] = "Sync";
            request = writer.write(req_t);
            auth = true;
            ssl_success = auth && isSSL;
            refused = false;
            return true;
        } else if (!strcmp(cmd,"Retrieved")) {
            for (auto itr : root[i]["keys"].getMemberNames()) {
                if (!map_server_data.count(itr)) continue;
                AP_GetServerDataRequest* target = map_server_data[itr];
                switch (target->type) {
                    case AP_DataType::Int:
                        *((int*)target->value) = root[i]["keys"][itr].asInt();
                        break;
                    case AP_DataType::Double:
                        *((double*)target->value) = root[i]["keys"][itr].asDouble();
                        break;
                    case AP_DataType::Raw:
                        *((std::string*)target->value) = writer.write(root[i]["keys"][itr]);
                        break;
                }
                target->status = AP_RequestStatus::Done;
                map_server_data.erase(itr);
            }
        } else if (!strcmp(cmd,"SetReply")) {
            if (setreplyfunc) {
                int int_val;
                int int_orig_val;
                double dbl_val;
                double dbl_orig_val;
                std::string raw_val;
                std::string raw_orig_val;
                AP_SetReply setreply;
                setreply.key = root[i]["key"].asString();
                switch (map_serverdata_typemanage[setreply.key]) {
                    case AP_DataType::Int:
                        int_val = root[i]["value"].asInt();
                        int_orig_val = root[i]["original_value"].asInt();
                        setreply.value = &int_val;
                        setreply.original_value = &int_orig_val;
                        break;
                    case AP_DataType::Double:
                        dbl_val = root[i]["value"].asDouble();
                        dbl_orig_val = root[i]["original_value"].asDouble();
                        setreply.value = &dbl_val;
                        setreply.original_value = &dbl_orig_val;
                        break;
                    default:
                        raw_val = root[i]["value"].asString();
                        raw_orig_val = root[i]["original_value"].asString();
                        setreply.value = &raw_val;
                        setreply.original_value = &raw_orig_val;
                        break;
                }
                (*setreplyfunc)(setreply);
            }
        } else if (!strcmp(cmd,"PrintJSON")) {
            if (!strcmp(root[i].get("type","").asCString(),"ItemSend")) {
                if (map_player_id_alias.at(root[i]["receiving"].asInt()) == map_player_id_alias[ap_player_id] || map_player_id_alias.at(root[i]["item"]["player"].asInt()) != map_player_id_alias[ap_player_id]) continue;
                AP_ItemSendMessage* msg = new AP_ItemSendMessage;
                msg->type = AP_MessageType::ItemSend;
                msg->item = getItemName(root[i]["item"]["item"].asInt64());
                msg->recvPlayer = map_player_id_alias.at(root[i]["receiving"].asInt());
                msg->text = msg->item + std::string(" was sent to ") + msg->recvPlayer;
                messageQueue.push_back(msg);
            } else if(!strcmp(root[i].get("type","").asCString(),"Hint")) {
                AP_HintMessage* msg = new AP_HintMessage;
                msg->type = AP_MessageType::Hint;
                msg->item = getItemName(root[i]["item"]["item"].asInt64());
                msg->sendPlayer = map_player_id_alias.at(root[i]["item"]["player"].asInt());
                msg->recvPlayer = map_player_id_alias.at(root[i]["receiving"].asInt());
                msg->location = getLocationName(root[i]["item"]["location"].asInt64());
                msg->checked = root[i]["found"].asBool();
                msg->text = std::string("Item ") + msg->item + std::string(" from ") + msg->sendPlayer + std::string(" to ") + msg->recvPlayer + std::string(" at ") + msg->location + std::string((msg->checked ? " (Checked)" : " (Unchecked)"));
                messageQueue.push_back(msg);
            } else if (!strcmp(root[i].get("type","").asCString(),"Countdown")) {
                AP_CountdownMessage* msg = new AP_CountdownMessage;
                msg->type = AP_MessageType::Countdown;
                msg->timer = root[i]["countdown"].asInt();
                msg->text = root[i]["data"][0]["text"].asString();
                messageQueue.push_back(msg);
            } else {
                AP_Message* msg = new AP_Message;
                msg->text = "";
                for (auto itr : root[i]["data"]) {
                    if (itr.get("type","").asString() == "player_id") {
                        msg->text += map_player_id_alias[itr["text"].asInt()];
                    } else if (itr.get("type","").asString() == "item_id") {
                        msg->text += getItemName(itr["text"].asInt64());
                    } else if (itr.get("type","").asString() == "location_id") {
                        msg->text += getLocationName(itr["text"].asInt64());
                    } else if (itr.get("text","") != "") {
                        msg->text += itr["text"].asString();
                    }
                }
                messageQueue.push_back(msg);
            }
        } else if (!strcmp(cmd, "LocationInfo")) {
            //Uninteresting for now.
        } else if (!strcmp(cmd, "ReceivedItems")) {
            int item_idx = root[i]["index"].asInt();
            bool notify;
            for (unsigned int j = 0; j < root[i]["items"].size(); j++) {
                int64_t item_id = root[i]["items"][j]["item"].asInt64();
                notify = (item_idx == 0 && last_item_idx <= j && multiworld) || item_idx != 0;
                (*getitemfunc)(item_id, notify);
                if (queueitemrecvmsg && notify) {
                    AP_ItemRecvMessage* msg = new AP_ItemRecvMessage;
                    msg->type = AP_MessageType::ItemRecv;
                    msg->item = getItemName(item_id);
                    msg->sendPlayer = map_player_id_alias.at(root[i]["items"][j]["player"].asInt());
                    msg->text = std::string("Received ") + msg->item + std::string(" from ") + msg->sendPlayer;
                    messageQueue.push_back(msg);
                }
            }
            last_item_idx = item_idx == 0 ? root[i]["items"].size() : last_item_idx + root[i]["items"].size();
            AP_SetServerDataRequest request;
            request.key = "APCppLastRecv" + ap_player_name + std::to_string(ap_player_id);
            AP_DataStorageOperation replac;
            replac.operation = "replace";
            replac.value = &last_item_idx;
            std::vector<AP_DataStorageOperation> operations;
            operations.push_back(replac);
            request.operations = operations;
            request.default_value = 0;
            request.type = AP_DataType::Int;
            request.want_reply = false;
            AP_SetServerData(&request);
        } else if (!strcmp(cmd, "RoomUpdate")) {
            //Sync checks with server
            for (unsigned int j = 0; j < root[i]["checked_locations"].size(); j++) {
                int64_t loc_id = root[i]["checked_locations"][j].asInt64();
                (*checklocfunc)(loc_id);
            }
            //Update Player aliases if present
            for (auto itr : root[i].get("players", Json::arrayValue)) {
                map_player_id_alias[itr["slot"].asInt()] = itr["alias"].asString();
            }
        } else if (!strcmp(cmd, "ConnectionRefused")) {
            auth = false;
            refused = true;
            printf("AP: Archipelago Server has refused connection. Check Password / Name / IP and restart the Game.\n");
            fflush(stdout);
        } else if (!strcmp(cmd, "Bounced")) {
            // Only expected Packages are DeathLink Packages. RIP
            if (!enable_deathlink) continue;
            for (unsigned int j = 0; j < root[i]["tags"].size(); j++) {
                if (!strcmp(root[i]["tags"][j].asCString(), "DeathLink")) {
                    // Suspicions confirmed ;-; But maybe we died, not them?
                    if (!strcmp(root[i]["data"]["source"].asCString(), ap_player_name.c_str())) break; // We already paid our penance
                    deathlinkstat = true;
                    std::string out = root[i]["data"]["source"].asString() + " killed you";
                    if (recvdeath != nullptr) {
                        (*recvdeath)();
                    }
                    break;
                }
            }
        }
    }
    return false;
}

void APSend(std::string req) {
    if (webSocket.getReadyState() != ix::ReadyState::Open) {
        printf("AP: Not Connected. Send will fail.\n");
        return;
    }
    webSocket.send(req);
}

void WriteSPSave() {
    sp_save_file.seekp(0);
    sp_save_file << writer.write(sp_save_root).c_str();
    sp_save_file.flush();
}

std::string getItemName(int64_t id) {
    return map_item_id_name.count(id) ? map_item_id_name.at(id) : std::string("Unknown Item") + std::to_string(id);
}

std::string getLocationName(int64_t id) {
    return map_location_id_name.count(id) ? map_location_id_name.at(id) : std::string("Unknown Location") + std::to_string(id);
}