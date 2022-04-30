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
#include <vector>

//Setup Stuff
bool init = false;
bool auth = false;
bool multiworld = true;
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
std::deque<AP_Message*> messageQueue;
std::map<int, std::string> map_player_id_name;
std::map<int, std::string> map_location_id_name;
std::map<int, std::string> map_item_id_name;

//Callback function pointers
void (*resetItemValues)();
void (*getitemfunc)(int,bool);
void (*checklocfunc)(int);
void (*recvdeath)() = nullptr;

bool queueitemrecvmsg = true;

int last_item_idx = 0;

std::ofstream sp_save_file;
Json::Value sp_save_root;

//Misc Data for Clients
AP_RoomInfo lib_room_info;

//Server Data Stuff
std::map<std::string, AP_GetServerDataRequest*> map_server_data;
std::chrono::steady_clock::time_point last_send_req = std::chrono::steady_clock::now();

//Slot Data Stuff
std::map<std::string, void (*)(int)> map_slotdata_callback_int;
std::map<std::string, void (*)(std::map<int,int>)> map_slotdata_callback_mapintint;
std::vector<std::string> slotdata_strings;

ix::WebSocket webSocket;
Json::Reader reader;
Json::FastWriter writer;

Json::Value sp_ap_root;

bool parse_response(std::string msg, std::string &request);
void APSend(std::string req);
void WriteSPSave();

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
    std::string url("ws://" + ap_ip);
    webSocket.setUrl(url);
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
                printf("AP: Error connecting to Archipelago. Retries: %d\n", msg->errorInfo.retries-1);
            }
        }
    );
    webSocket.setPingInterval(45);

    map_player_id_name[0] = "Archipelago";
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
        fake_msg[0]["slot"] = 1404;
        fake_msg[0]["players"] = Json::arrayValue;
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
            item["item"] = sp_ap_root["location_to_item"][sp_save_root["checked_locations"][i].asString()].asInt();
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

void AP_SendItem(int idx) {
    if (map_location_id_name.count(idx)) {
        printf(("AP: Checked " + map_location_id_name.at(idx) + ". Informing Archipelago...\n").c_str());
    } else {
        printf("AP: Checked unknown location %d. Informing Archipelago...\n", idx);
    }
    if (multiworld) {
        Json::Value req_t;
        req_t[0]["cmd"] = "LocationChecks";
        req_t[0]["locations"][0] = idx;
        APSend(writer.write(req_t));
    } else {
        for (auto itr : sp_save_root["checked_locations"]) {
            if (itr.asInt() == idx) {
                return;
            }
        }
        int recv_item_id = sp_ap_root["location_to_item"].get(std::to_string(idx), 0).asInt();
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

void AP_SetItemRecvCallback(void (*f_itemrecv)(int,bool)) {
    getitemfunc = f_itemrecv;
}

void AP_SetLocationCheckedCallback(void (*f_locrecv)(int)) {
    checklocfunc = f_locrecv;
}

void AP_SetDeathLinkRecvCallback(void (*f_deathrecv)()) {
    recvdeath = f_deathrecv;
}

void AP_RegisterSlotDataIntCallback(std::string key, void (*f_slotdata)(int)) {
    map_slotdata_callback_int[key] = f_slotdata;
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
                int loc_id = root[i]["checked_locations"][j].asInt();
                (*checklocfunc)(loc_id);
            }
            for (unsigned int j = 0; j < root[i]["players"].size(); j++) {
                map_player_id_name.insert(std::pair<int,std::string>(root[i]["players"][j]["slot"].asInt(),root[i]["players"][j]["alias"].asString()));
            }
            if (root[i]["slot_data"].get("DeathLink", false).asBool() && deathlinksupported) enable_deathlink = true;
            deathlink_amnesty = root[i]["slot_data"].get("DeathLink_Amnesty", 0).asInt();
            cur_deathlink_amnesty = deathlink_amnesty;
            for (std::string key : slotdata_strings) {
                if (map_slotdata_callback_int.count(key)) {
                    (*map_slotdata_callback_int.at(key))(root[i]["slot_data"][key].asInt());
                } else if (map_slotdata_callback_mapintint.count(key)) {
                    std::map<int,int> out;
                    for (auto itr : root[i]["slot_data"][key].getMemberNames()) {
                        out[std::stoi(itr)] = root[i]["slot_data"][key][itr.c_str()].asInt();
                    }
                    (*map_slotdata_callback_mapintint.at(key))(out);
                }
                
            }

            AP_GetServerDataRequest serverdata_request;
            serverdata_request.key = "APCppLastRecv" + ap_player_name + std::to_string(ap_player_id);
            serverdata_request.data = &last_item_idx;
            serverdata_request.type = AP_DataType::Int;
            AP_GetServerData(&serverdata_request);

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
                    map_item_id_name[itr["item_name_to_id"][itr2].asInt()] = itr2;
                }
                for (auto itr2 : itr["location_name_to_id"].getMemberNames()) {
                    map_location_id_name[itr["location_name_to_id"][itr2].asInt()] = itr2;
                }
            }
            Json::Value req_t;
            req_t[0]["cmd"] = "Sync";
            request = writer.write(req_t);
            auth = true;
            return true;
        } else if (!strcmp(cmd,"Retrieved")) {
            for (auto itr : root[i]["keys"].getMemberNames()) {
                AP_GetServerDataRequest* target = map_server_data[itr];
                switch (target->type) {
                    case AP_DataType::Int:
                        *((int*)target->data) = root[i]["keys"][itr].asInt();
                        break;
                    case AP_DataType::Raw:
                        *((std::string*)target->data) = writer.write(root[i]["keys"][itr]);
                        break;
                }
                target->status = AP_RequestStatus::Done;
                map_server_data.erase(itr);
            }
        } else if (!strcmp(cmd,"PrintJSON")) {
            if (!strcmp(root[i].get("type","").asCString(),"ItemSend")) {
                if (map_player_id_name.at(root[i]["receiving"].asInt()) == ap_player_name || map_player_id_name.at(root[i]["item"]["player"].asInt()) != ap_player_name) continue;
                AP_ItemSendMessage* msg = new AP_ItemSendMessage;
                msg->type = AP_MessageType::ItemSend;
                msg->item = map_item_id_name.at(root[i]["item"]["item"].asInt());
                msg->recvPlayer = map_player_id_name.at(root[i]["receiving"].asInt());
                msg->text = msg->item + std::string(" was sent to ") + msg->recvPlayer;
                messageQueue.push_back(msg);
            } else if(!strcmp(root[i].get("type","").asCString(),"Hint")) {
                AP_HintMessage* msg = new AP_HintMessage;
                msg->type = AP_MessageType::Hint;
                msg->item = map_item_id_name.at(root[i]["item"]["item"].asInt());
                msg->sendPlayer = map_player_id_name.at(root[i]["item"]["player"].asInt());
                msg->recvPlayer = map_player_id_name.at(root[i]["receiving"].asInt());
                msg->location = map_location_id_name.at(root[i]["item"]["location"].asInt());
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
                        msg->text += map_player_id_name[itr["text"].asInt()];
                    } else if (itr.get("type","").asString() == "item_id") {
                        msg->text += map_item_id_name[itr["text"].asInt()];
                    } else if (itr.get("type","").asString() == "location_id") {
                        msg->text += map_location_id_name[itr["text"].asInt()];
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
                int item_id = root[i]["items"][j]["item"].asInt();
                notify = (item_idx == 0 && last_item_idx <= j && multiworld) || item_idx != 0;
                (*getitemfunc)(item_id, notify);
                if (queueitemrecvmsg && notify) {
                    AP_ItemRecvMessage* msg = new AP_ItemRecvMessage;
                    msg->type = AP_MessageType::ItemRecv;
                    msg->item = map_item_id_name.at(item_id);
                    msg->sendPlayer = map_player_id_name.at(root[i]["items"][j]["player"].asInt());
                    msg->text = std::string("Received ") + msg->item + std::string(" from ") + msg->sendPlayer;
                    messageQueue.push_back(msg);
                }
            }
            last_item_idx = item_idx == 0 ? root[i]["items"].size() : last_item_idx + root[i]["items"].size();
            AP_SetServerDataRaw("APCppLastRecv" + ap_player_name + std::to_string(ap_player_id), "replace", std::to_string(last_item_idx), "");
        } else if (!strcmp(cmd, "RoomUpdate")) {
            for (unsigned int j = 0; j < root[i]["checked_locations"].size(); j++) {
                //Sync checks with server
                int loc_id = root[i]["checked_locations"][j].asInt();
                (*checklocfunc)(loc_id);
            }
        } else if (!strcmp(cmd, "ConnectionRefused")) {
            auth = false;
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

void AP_SetServerDataRaw(std::string key, std::string operation, std::string value, std::string default_val) {
    // Rate Limiting
    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_send_req).count() < 20) {
        return;
    }
    last_send_req = std::chrono::steady_clock::now();

    Json::Value data;
    reader.parse(value, data);

    Json::Value req_t;
    req_t[0]["cmd"] = "Set";
    req_t[0]["key"] = key;
    if (!default_val.empty()) {
        Json::Value default_val_json;
        reader.parse(default_val, default_val_json);
        req_t[0]["default"] = default_val_json;
    }
    req_t[0]["operations"][0]["operation"] = operation;
    req_t[0]["operations"][0]["value"] = data;
    APSend(writer.write(req_t));
}

void AP_GetServerData(AP_GetServerDataRequest* request) {
    if (map_server_data.find(request->key) != map_server_data.end()) return;

    map_server_data[request->key] = request;
    request->status = Pending;

    Json::Value req_t;
    req_t[0]["cmd"] = "Get";
    req_t[0]["keys"][0] = request->key;
    APSend(writer.write(req_t));
}

// PRIV

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