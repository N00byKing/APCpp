#include "Archipelago.h"
#include <json/json.h>
#include <json/value.h>
#include <json/writer.h>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

extern Json::FastWriter writer;
extern Json::Reader reader;
extern std::mt19937_64 rando;
extern int ap_player_id;
extern int ap_player_team;
extern std::set<int> teams_set;
extern std::map<int, std::string> map_player_id_alias;
extern std::map<std::string, int> map_player_alias_id;

// Stuff that is only used for Gifting
std::map<std::pair<int,std::string>,AP_GiftBoxProperties> map_players_to_giftbox;
std::map<std::string,AP_Gift> cur_gifts_available;

// PRIV Func Declarations Start
AP_RequestStatus sendGiftInternal(AP_Gift gift);
// PRIV Func Declarations End

#define AP_PLAYER_GIFTBOX_KEY ("GiftBox;" + std::to_string(ap_player_team) + ";" + std::to_string(ap_player_id))

void AP_SetGiftBoxProperties(AP_GiftBoxProperties props) {
    // Create Local Box if needed
    AP_SetServerDataRequest req_local_box;
    req_local_box.key = AP_PLAYER_GIFTBOX_KEY;
    std::string LocalGiftBoxDef_s = writer.write(Json::objectValue);
    req_local_box.operations = {{"default", &LocalGiftBoxDef_s}};
    req_local_box.type = AP_DataType::Raw;
    req_local_box.want_reply = true;
    AP_BulkSetServerData(&req_local_box);

    // Set Properties
    Json::Value GlobalGiftBox = Json::objectValue;
    GlobalGiftBox[std::to_string(ap_player_id)]["IsOpen"] = props.IsOpen;
    GlobalGiftBox[std::to_string(ap_player_id)]["AcceptsAnyGift"] = props.AcceptsAnyGift;
    GlobalGiftBox[std::to_string(ap_player_id)]["DesiredTraits"] = Json::arrayValue;
    for (std::string trait_s : props.DesiredTraits)
        GlobalGiftBox[std::to_string(ap_player_id)]["DesiredTraits"].append(trait_s);
    GlobalGiftBox[std::to_string(ap_player_id)]["MinimumGiftDataVersion"] = 2;
    GlobalGiftBox[std::to_string(ap_player_id)]["MaximumGiftDataVersion"] = 2;

    // Update entry in MotherBox
    AP_SetServerDataRequest req_global_box;
    req_global_box.key = "GiftBoxes;" + std::to_string(ap_player_team);
    std::string GlobalGiftBox_s = writer.write(GlobalGiftBox);
    Json::Value DefBoxGlobal;
    DefBoxGlobal[std::to_string(ap_player_id)] = Json::objectValue;
    std::string DefBoxGlobal_s = writer.write(DefBoxGlobal);
    req_global_box.operations = {
        {"default", &DefBoxGlobal_s},
        {"update", &GlobalGiftBox_s}};
    req_global_box.default_value = &DefBoxGlobal_s;
    req_global_box.type = AP_DataType::Raw;
    req_global_box.want_reply = false;
    AP_BulkSetServerData(&req_global_box);

    // Set Values
    AP_CommitServerData();
    while (req_local_box.status == AP_RequestStatus::Pending && req_global_box.status == AP_RequestStatus::Pending && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {}
}

std::map<std::pair<int,std::string>,AP_GiftBoxProperties> AP_QueryGiftBoxes() {
    std::map<std::pair<int,std::string>,AP_GiftBoxProperties> res;
    std::map<int,std::string> team_data;
    std::map<int,AP_GetServerDataRequest> team_reqs;
    for (int team : teams_set) {
        team_data[team] = "";
        // Send and wait for data
        team_reqs[team].key = "GiftBoxes;" + std::to_string(team);
        team_reqs[team].type = AP_DataType::Raw;
        team_reqs[team].value = &team_data[team];
        AP_BulkGetServerData(&team_reqs[team]);
    }
    AP_CommitServerData();
    
    while (true && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {
        bool done = true;
        for (std::pair<int,AP_GetServerDataRequest> req : team_reqs) {
            if (req.second.status == AP_RequestStatus::Pending) {
                done = false;
                break;
            }
        }
        if (done) break;
    }

    if (AP_GetConnectionStatus() != AP_ConnectionStatus::Authenticated) return res; // Connection Loss occured

    // Write back data if present
    for (int team : teams_set) {
        if (team_reqs[team].status == AP_RequestStatus::Error) continue; // Might just be that noone set it up yet.
        Json::Value json_data;
        reader.parse(team_data[team], json_data);
        for(std::string motherbox_slot : json_data.getMemberNames()) {
            int slot = atoi(motherbox_slot.c_str());
            Json::Value team_root = json_data[std::to_string(team)];
            res[{team,map_player_id_alias[slot]}].IsOpen = team_root.get("IsOpen",false).asBool();
            res[{team,map_player_id_alias[slot]}].AcceptsAnyGift = team_root.get("AcceptsAnyGift",false).asBool();
            std::vector<std::string> DesiredTraits;
            for (int i = 0; i < team_root["DesiredTraits"].size(); i++) {
                DesiredTraits.push_back(team_root["DesiredTraits"][i].asString());
            }
            res[{team,map_player_id_alias[slot]}].DesiredTraits = DesiredTraits;
        }
    }
    map_players_to_giftbox = res;
    return res;
}

// Get currently available Gifts in own gift box
std::map<std::string,AP_Gift> AP_CheckGifts() {
    return cur_gifts_available;
}

AP_RequestStatus AP_SendGift(AP_Gift gift) {
    if (gift.IsRefund) return AP_RequestStatus::Error;
    if (map_players_to_giftbox[{gift.ReceiverTeam, gift.Receiver}].IsOpen == true) {
        return sendGiftInternal(gift);
    }
    return AP_RequestStatus::Error;
}

AP_RequestStatus AP_AcceptGift(std::string id, AP_Gift* gift) {
    if (cur_gifts_available.count(id)) {
        AP_SetServerDataRequest req;
        req.key = AP_PLAYER_GIFTBOX_KEY;
        req.type = AP_DataType::Raw;
        req.want_reply = true;
        req.operations = {
            {"pop", &gift->ID}
        };
        AP_SetServerData(&req);
        while (req.status == AP_RequestStatus::Pending && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {}
        if (req.status == AP_RequestStatus::Done) {
            *gift = cur_gifts_available.at(id);
            cur_gifts_available.erase(id);
            return req.status;
        }
    }
    return AP_RequestStatus::Error;
}

AP_RequestStatus AP_RejectGift(std::string id) {
    if (cur_gifts_available.count(id)) {
        AP_Gift gift;
        AP_RequestStatus status = AP_AcceptGift(id, &gift);
        if (status == AP_RequestStatus::Error) {
            return status;
        }
        gift.IsRefund = true;
        return sendGiftInternal(gift);
    }
    return AP_RequestStatus::Error;
}

// PRIV
void handleGiftAPISetReply(AP_SetReply reply) {
    if (reply.key == AP_PLAYER_GIFTBOX_KEY) {
        Json::Value local_giftbox;
        reader.parse(*(std::string*)reply.value, local_giftbox);
        for (std::string gift_id : local_giftbox.getMemberNames()) {
            AP_Gift gift;
            gift.ID = gift_id;
            gift.ItemName = local_giftbox[gift_id].get("ItemName", "Unknown").asString();
            gift.Amount = local_giftbox[gift_id].get("Amount", 0).asUInt();
            gift.ItemValue = local_giftbox[gift_id].get("ItemValue", 0).asUInt();
            for (Json::Value trait_v : local_giftbox[gift_id]["Traits"]) {
                AP_GiftTrait trait;
                trait.Trait = trait_v.get("Trait", "Unknown").asString();
                trait.Quality = trait_v.get("Quality", 1.).asDouble();
                trait.Duration = trait_v.get("Duration", 1.).asDouble();
                gift.Traits.push_back(trait);
            }
            gift.Sender = map_player_id_alias[local_giftbox[gift_id]["SenderSlot"].asInt()];
            gift.Receiver = map_player_id_alias[local_giftbox[gift_id]["ReceiverSlot"].asInt()];
            gift.SenderTeam = local_giftbox[gift_id].get("SenderTeam", 0).asInt();
            gift.ReceiverTeam = local_giftbox[gift_id].get("ReceiverTeam", 0).asInt();
            gift.IsRefund = local_giftbox[gift_id].get("IsRefund", false).asBool();
            cur_gifts_available[gift_id] = gift;
        }
    }
}

AP_RequestStatus sendGiftInternal(AP_Gift gift) {
    std::string giftbox_key = "GiftBox;";
    if (!gift.IsRefund)
        giftbox_key += std::to_string(gift.ReceiverTeam) + ";" + std::to_string(map_player_alias_id[gift.Receiver]);
    else
        giftbox_key += std::to_string(gift.SenderTeam) + ";" + std::to_string(map_player_alias_id[gift.Sender]);

    Json::Value giftVal;
    if (gift.IsRefund) {
        giftVal["ID"] = gift.ID;
    } else {
        uint64_t random1 = rando();
        uint64_t random2 = rando();
        std::ostringstream id;
        id << std::hex << std::to_string(random1) << std::to_string(random2);
        giftVal["ID"] = id.str();
    }
    giftVal["ItemName"] = gift.ItemName;
    giftVal["Amount"] = gift.Amount;
    giftVal["ItemValue"] = gift.ItemValue;
    for (AP_GiftTrait trait : gift.Traits) {
        Json::Value trait_v;
        trait_v["Trait"] = trait.Trait;
        trait_v["Quality"] = trait.Quality;
        trait_v["Duration"] = trait.Duration;
        giftVal["Traits"].append(trait_v);
    }
    giftVal["SenderSlot"] = map_player_alias_id[gift.Sender];
    giftVal["ReceiverSlot"] = map_player_alias_id[gift.Receiver];
    giftVal["SenderTeam"] = gift.SenderTeam;
    giftVal["ReceiverTeam"] = gift.ReceiverTeam;
    giftVal["IsRefund"] = gift.IsRefund;
    std::string gift_s = writer.write(giftVal);

    AP_SetServerDataRequest req;
    req.key = giftbox_key;
    req.type = AP_DataType::Raw;
    req.want_reply = false;
    Json::Value defVal = Json::objectValue;
    std::string defVal_s = writer.write(defVal);
    req.default_value = &defVal_s;

    req.operations = {
        {"default", &defVal_s},
        {"push", &gift_s}
    };

    AP_SetServerData(&req);
    while (req.status == AP_RequestStatus::Pending && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {}

    return req.status;
}
