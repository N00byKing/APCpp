#pragma once

#include <string>
#include <vector>
#include <map>

void AP_Init(const char*, const char*, const char*, const char*);
void AP_Init(const char*);
bool AP_IsInit();

void AP_Start();

struct AP_NetworkVersion {
    int major;
    int minor;
    int build;
};

// Set current client version
void AP_SetClientVersion(AP_NetworkVersion*);

#define AP_PERMISSION_DISABLED 0b000
#define AP_PERMISSION_ENABLED 0b001
#define AP_PERMISSION_GOAL 0b010
#define AP_PERMISSION_AUTO 0b110

enum AP_ConnectionStatus {
    Disconnected, Connected, Authenticated
};

struct AP_RoomInfo {
    AP_NetworkVersion version;
    std::vector<std::string> tags;
    bool password_required;
    std::map<std::string, int> permissions;
    int hint_cost;
    int location_check_points;
    //MISSING: players
    //MISSING: games
    int datapackage_version;
    std::map<std::string, int> datapackage_versions;
    std::string seed_name;
    float time;
};

int AP_GetRoomInfo(AP_RoomInfo*);

void AP_EnableQueueItemRecvMsgs(bool);

void AP_SetDeathLinkSupported(bool);

//Parameter Function must reset local state
void AP_SetItemClearCallback(void (*f_itemclr)());
//Parameter Function must collect item id given with parameter. Secound parameter indicates whether or not to notify player
void AP_SetItemRecvCallback(void (*f_itemrecv)(int,bool));
//Parameter Function must mark given location id as checked
void AP_SetLocationCheckedCallback(void (*f_locrecv)(int));
//Parameter Function will be called when Death Link is received. Alternative to Pending/Clear usage
void AP_SetDeathLinkRecvCallback(void (*f_deathrecv)());

void AP_RegisterSlotDataIntCallback(std::string, void (*f_slotdata)(int));
void AP_RegisterSlotDataMapIntIntCallback(std::string, void (*f_slotdata)(std::map<int,int>));

// Sends LocationCheck for given index
void AP_SendItem(int);

bool AP_DeathLinkPending();
void AP_DeathLinkClear();
void AP_DeathLinkSend();

// Called when Story completed, sends StatusUpdate
void AP_StoryComplete();

enum AP_MessageType {
    Plaintext, ItemSend, ItemRecv, Hint, Countdown
};

struct AP_Message {
    AP_MessageType type = AP_MessageType::Plaintext;
    std::string text;
};

struct AP_ItemSendMessage : AP_Message {
    std::string item;
    std::string recvPlayer;
};

struct AP_ItemRecvMessage : AP_Message {
    std::string item;
    std::string sendPlayer;
};

struct AP_HintMessage : AP_Message {
    std::string item;
    std::string sendPlayer;
    std::string recvPlayer;
    std::string location;
    bool checked;
};

struct AP_CountdownMessage : AP_Message {
    int timer;
};

bool AP_IsMessagePending();
void AP_ClearLatestMessage();
AP_Message* AP_GetLatestMessage();

AP_ConnectionStatus AP_GetConnectionStatus();
