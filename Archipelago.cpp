#include "Archipelago.h"

#include "ixwebsocket/IXNetSystem.h"
#include "ixwebsocket/IXWebSocket.h"
#include "ixwebsocket/IXUserAgent.h"

#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#include <deque>
#include <string>
#include <chrono>
#include <cstdlib>

#define ADD_TO_MSGQUEUE(x,y) messageQueue.push_back(std::pair<std::string,int>(x,y))

bool init = false;
bool auth = false;
int ap_player_id;
std::string ap_player_name;
std::string ap_ip;
std::string ap_game;
std::string ap_passwd;
int ap_uuid = 0;
std::deque<std::pair<std::string,int>> messageQueue;
std::map<int, std::string> map_player_id_name;
std::map<int, std::string> map_location_id_name;
std::map<int, std::string> map_item_id_name;

void (*resetItemValues)();
void (*getitemfunc)(int);

ix::WebSocket webSocket;
Json::Reader reader;
Json::FastWriter writer;

bool parse_response(std::string msg, std::string &request);
void APSend(std::string req);

void AP_Init(const char* ip, const char* game, const char* player_name, const char* passwd) {
    if (init) {
        return;
    }

    std::srand(std::time(nullptr)); // use current time as seed for random generator

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
            else if (msg->type == ix::WebSocketMessageType::Error)
            {
                auth = false;
                printf("AP: Error connecting to Archipelago. Retries: %d\n", msg->errorInfo.retries-1);
            }
        }
    );
}

void AP_Start() {
    init = true;
    webSocket.start();
}

bool AP_IsInit() {
    return init;
}

void AP_SendItem(int idx) {
    if (!auth) {
        printf("AP: Not Connected. Send will fail.\n");
        return;
    }
    printf(("AP: Checked " + map_location_id_name.at(idx) + ". Informing Archipelago...\n").c_str());
    Json::Value req_t;
    req_t[0]["cmd"] = "LocationChecks";
    req_t[0]["locations"][0] = idx;
    APSend(writer.write(req_t));
}

void AP_StoryComplete() {
    if (auth) {
        Json::Value req_t;
        req_t[0]["cmd"] = "StatusUpdate";
        req_t[0]["status"] = 30; //CLIENT_GOAL
        APSend(writer.write(req_t));
    } else {
        printf("AP: Not Connected. Send will fail.\n");
    }
}

std::deque<std::pair<std::string,int>> AP_GetMsgQueue() {
    return messageQueue;
}

void AP_SetItemClearCallback(void (*f_itemclr)()) {
    resetItemValues = f_itemclr;
}

void AP_SetItemRecvCallback(void (*f_itemrecv)(int)) {
    getitemfunc = f_itemrecv;
}

bool parse_response(std::string msg, std::string &request) {
    Json::Value root;
    reader.parse(msg, root);
    for (unsigned int i = 0; i < root.size(); i++) {
        const char* cmd = root[0]["cmd"].asCString();
        if (!strcmp(cmd,"RoomInfo")) {
            if (!auth) {
                Json::Value req_t;
                ap_uuid = std::rand();
                req_t[i]["cmd"] = "Connect";
                req_t[i]["game"] = ap_game;
                req_t[i]["name"] = ap_player_name;
                req_t[i]["password"] = ap_passwd;
                req_t[i]["uuid"] = ap_uuid;
                req_t[i]["tags"][0] = "DeathLink"; // Send Tag even though we don't know if we want these packages, just in case
                req_t[i]["version"]["major"] = "0";
                req_t[i]["version"]["minor"] = "2";
                req_t[i]["version"]["build"] = "2";
                req_t[i]["version"]["class"] = "Version";
                request = writer.write(req_t);
                auth = false;
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
                #warning todo
            }
            for (unsigned int j = 0; j < root[i]["players"].size(); j++) {
                map_player_id_name.insert(std::pair<int,std::string>(root[i]["players"][j]["slot"].asInt(),root[i]["players"][j]["alias"].asString()));
            }
            Json::Value req_t;
            req_t[0]["cmd"] = "GetDataPackage";
            request = writer.write(req_t);
            return true;
        } else if (!strcmp(cmd,"DataPackage")) {
            for (unsigned int j = 0; j < root[i]["data"]["games"].size(); j++) {
                for (auto itr : root[i]["data"]["games"]) {
                    for (auto itr2 : itr["item_name_to_id"].getMemberNames()) {
                        map_item_id_name.insert(std::pair<int,std::string>(itr["item_name_to_id"][itr2.c_str()].asInt(), itr2));
                    }
                    for (auto itr2 : itr["location_name_to_id"].getMemberNames()) {
                        map_location_id_name.insert(std::pair<int,std::string>(itr["location_name_to_id"][itr2.c_str()].asInt(), itr2));
                    }
                }
            }
            Json::Value req_t;
            req_t[0]["cmd"] = "Sync";
            request = writer.write(req_t);
            auth = true;
            return true;
        } else if (!strcmp(cmd,"Print")) {
            printf("AP: %s\n", root[i]["text"].asCString());
        } else if (!strcmp(cmd,"PrintJSON")) {
            if (!strcmp(root[i].get("type","").asCString(),"ItemSend")) {
                if (map_player_id_name.at(root[i]["receiving"].asInt()) == ap_player_name || map_player_id_name.at(root[i]["item"]["player"].asInt()) != ap_player_name) continue;
                printf(map_player_id_name.at(root[i]["item"]["player"].asInt()).c_str());
                ADD_TO_MSGQUEUE((map_item_id_name.at(root[i]["item"]["item"].asInt()) + " was sent"), 1);
                ADD_TO_MSGQUEUE(("to " + map_player_id_name.at(root[i]["receiving"].asInt())), 0);
                printf("AP: Item from %s to %s\n", map_player_id_name.at(root[i]["item"]["player"].asInt()).c_str(), map_player_id_name.at(root[i]["receiving"].asInt()).c_str());
            } else if(!strcmp(root[i].get("type","").asCString(),"Hint")) {
                printf("AP: Hint: Item %s from %s to %s at %s %s\n", map_item_id_name.at(root[i]["item"]["item"].asInt()).c_str(), map_player_id_name.at(root[i]["item"]["player"].asInt()).c_str(),
                                                                 map_player_id_name.at(root[i]["receiving"].asInt()).c_str(), map_location_id_name.at(root[i]["item"]["location"].asInt()).c_str(),
                                                                 (root[i]["found"].asBool() ? " (Checked)" : " (Unchecked)"));
            }
        } else if (!strcmp(cmd, "LocationInfo")) {
            //Uninteresting for now.
        } else if (!strcmp(cmd, "ReceivedItems")) {
            for (unsigned int j = 0; j < root[i]["items"].size(); j++) {
                int item_id = root[i]["items"][j]["item"].asInt();
                (*getitemfunc)(item_id);
                ADD_TO_MSGQUEUE(map_item_id_name.at(item_id) + " received", 1);
                ADD_TO_MSGQUEUE(("From " + map_player_id_name.at(root[i]["items"][j]["player"].asInt())), 0);
            }
        } else if (!strcmp(cmd, "RoomUpdate")) {
            for (unsigned int j = 0; j < root[i]["checked_locations"].size(); j++) {
                //Sync checks with server
                int loc_id = root[i]["checked_locations"][j].asInt();
                #warning TODO Sync Checks
            }
        } else if (!strcmp(cmd, "ConnectionRefused")) {
            auth = false;
            printf("AP: Archipelago Server has refused connection. Check Password / Name / IP and restart the Game.");
            webSocket.stop();
        } else if (!strcmp(cmd, "Bounced")) {
            // None expected. Ignoring
        }
        
        else {
            printf("AP: Unhandled Packet. Command: %s\n", cmd);
        }
    }
    return false;
}

void APSend(std::string req) {
    webSocket.send(req);
}