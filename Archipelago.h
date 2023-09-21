#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <set>

void AP_Init(const char*, const char*, const char*, const char*);
void AP_Init(const char*);
bool AP_IsInit();

void AP_Start();

// AP_Shutdown resets the library state to before initialization, and doesn't just disconnect!
void AP_Shutdown();

struct AP_NetworkVersion {
    int major;
    int minor;
    int build;
};

struct AP_NetworkItem {
    int64_t item;
    int64_t location;
    int player;
    int flags;
    std::string itemName;
    std::string locationName;
    std::string playerName;
};

struct AP_NetworkPlayer {
    int team;
    int slot;
    std::string name;
    std::string alias;
    std::string game;
};

// Set current client version
void AP_SetClientVersion(AP_NetworkVersion*);

/* Configuration Functions */

void AP_EnableQueueItemRecvMsgs(bool);

void AP_SetDeathLinkSupported(bool);

/* Required Callback Functions */

//Parameter Function must reset local state
void AP_SetItemClearCallback(void (*f_itemclr)());
//Parameter Function must collect item id given with parameter. Secound parameter indicates whether or not to notify player
void AP_SetItemRecvCallback(void (*f_itemrecv)(int64_t,bool));
//Parameter Function must mark given location id as checked
void AP_SetLocationCheckedCallback(void (*f_locrecv)(int64_t));

/* Optional Callback Functions */

//Parameter Function will be called when Death Link is received. Alternative to Pending/Clear usage
void AP_SetDeathLinkRecvCallback(void (*f_deathrecv)());

// Parameter Function receives Slotdata of respective type
void AP_RegisterSlotDataIntCallback(std::string, void (*f_slotdata)(int));
void AP_RegisterSlotDataMapIntIntCallback(std::string, void (*f_slotdata)(std::map<int,int>));
void AP_RegisterSlotDataRawCallback(std::string, void (*f_slotdata)(std::string));

// Send LocationScouts packet
void AP_SendLocationScouts(std::set<int64_t> const& locations, int create_as_hint);
// Receive Function for LocationInfo
void AP_SetLocationInfoCallback(void (*f_locrecv)(std::vector<AP_NetworkItem>));

/* Game Management Functions */

// Sends LocationCheck for given index
void AP_SendItem(int64_t location);
void AP_SendItem(std::set<int64_t> const& locations);

// Called when Story completed, sends StatusUpdate
void AP_StoryComplete();

/* Deathlink Functions */

bool AP_DeathLinkPending();
void AP_DeathLinkClear();
void AP_DeathLinkSend();

/* Message Management Types */

enum struct AP_MessageType {
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

/* Message Management Functions */

bool AP_IsMessagePending();
void AP_ClearLatestMessage();
AP_Message* AP_GetLatestMessage();

void AP_Say(std::string);

/* Connection Information Types */

enum struct AP_ConnectionStatus {
    Disconnected, Connected, Authenticated, ConnectionRefused
};

#define AP_PERMISSION_DISABLED 0b000
#define AP_PERMISSION_ENABLED 0b001
#define AP_PERMISSION_GOAL 0b010
#define AP_PERMISSION_AUTO 0b110

struct AP_RoomInfo {
    AP_NetworkVersion version;
    std::vector<std::string> tags;
    bool password_required;
    std::map<std::string, int> permissions;
    int hint_cost;
    int location_check_points;
    //MISSING: games
    std::map<std::string, std::string> datapackage_checksums;
    std::string seed_name;
    double time;
};

/* Connection Information Functions */

int AP_GetRoomInfo(AP_RoomInfo*);
AP_ConnectionStatus AP_GetConnectionStatus();
int AP_GetUUID();
int AP_GetPlayerID();

/* Serverside Data Types */

enum struct AP_RequestStatus {
    Pending, Done, Error
};

enum struct AP_DataType {
    Raw, Int, Double
};

struct AP_GetServerDataRequest {
    AP_RequestStatus status;
    std::string key;
    void* value;
    AP_DataType type;
};

struct AP_DataStorageOperation {
    std::string operation;
    void* value;
};

struct AP_SetServerDataRequest {
    AP_RequestStatus status;
    std::string key;
    std::vector<AP_DataStorageOperation> operations;
    void* default_value = nullptr;
    AP_DataType type;
    bool want_reply;
};

struct AP_SetReply {
    std::string key;
    void* original_value;
    void* value;
};

struct AP_Bounce {
    std::vector<std::string>* games = nullptr; // Can be nullptr or empty, but must be set to either
    std::vector<std::string>* slots = nullptr; // Can be nullptr or empty, but must be set to either
    std::vector<std::string>* tags  = nullptr; // Can be nullptr or empty, but must be set to either
    std::string data; // Valid JSON Data. Can also be primitive (Numbers or literals)
};

/* Serverside Data Functions */

// Set and Receive Data
void AP_SetServerData(AP_SetServerDataRequest* request);
void AP_GetServerData(AP_GetServerDataRequest* request);

/* Set and Receive Data in bulk. These request will be queued up in a (shared) queue, and sent in one packet to Archipelago one one of the following is called:
 * - AP_CommitServerData()
 * - Either of AP_GetServerData() or AP_SetServerData()
 */
void AP_BulkSetServerData(AP_SetServerDataRequest* requests);
void AP_BulkGetServerData(AP_GetServerDataRequest* requests);

// Commits bulk server data requests
void AP_CommitServerData();

// This returns a string prefix, consistent across game connections and unique to the player slot.
// Intended to be used for getting / setting private server data
// No guarantees are made regarding the content of the prefix!
std::string AP_GetPrivateServerDataPrefix();

// Parameter Function receives all SetReply's
// ! Pointers in AP_SetReply struct only valid within function !
// If values are required beyond that a copy is needed
void AP_RegisterSetReplyCallback(void (*f_setreply)(AP_SetReply));

// Receive all SetReplys with Keys in parameter list
void AP_SetNotify(std::map<std::string,AP_DataType>);
// Single Key version of above for convenience
void AP_SetNotify(std::string, AP_DataType);

// Send Bounce package
void AP_SendBounce(AP_Bounce);

// Receive Bounced packages. Disables automatic DeathLink management
void AP_RegisterBouncedCallback(void (*f_bounced)(AP_Bounce));

/* Gifting API Types */

struct AP_GiftBoxProperties {
    bool IsOpen = false;
    bool AcceptsAnyGift = false;
    std::vector<std::string> DesiredTraits;
};

struct AP_GiftTrait {
    std::string Trait;
    double Quality = 1.;
    double Duration = 1.;
};

struct AP_Gift {
    std::string ID;
    std::string ItemName;
    #warning TODO clear up specs for Amount, ItemValue
    size_t Amount;
    size_t ItemValue;
    std::vector<AP_GiftTrait> Traits;
    std::string Sender;
    std::string Receiver;
    //int SenderTeam;
    //int ReceiverTeam;
    bool IsRefund;
};

/*
 * Gifting API Functions
 * 
 * These functions wrap around the DataStorage functions, but work in a blocking manner
 * They are only usable once authenticated. Be sure you are connected before using.
 * However, even if not all functions with possible data loss will report errors on connection loss.
 */

// Sets up Gift Box according to specifications given. Must be called at least once before sending / receiving gifts
void AP_SetGiftBoxProperties(AP_GiftBoxProperties props);

// Returns information on all Gift Boxes on the server as a map of <Team,PlayerName> -> GiftBoxProperties.
// This data is cached by the library, and attempting to send to someone who has no or a closed giftbox the last time this function was called will always fail
std::map<std::pair<int,std::string>,AP_GiftBoxProperties> AP_QueryGiftBoxes();

// Get currently available Gifts in own gift box
std::map<std::string,AP_GiftBoxProperties> AP_CheckGifts();

// Send a Gift.
AP_RequestStatus AP_SendItem(AP_Gift gift);

// Accept a gift from the Giftbox, and writes it into a struct in the second parameter. ONLY THIS DATA IS "REAL", DO NOT REUSE DATA FROM AP_CheckGifts()
AP_RequestStatus AP_AcceptGift(std::string id, AP_Gift* gift);

// Reject a gift from the Giftbox, and refund it.
AP_RequestStatus AP_RejectGift(std::string id);
