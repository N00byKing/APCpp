#include "Archipelago.h"
#include <json/json.h>
#include <json/value.h>
#include <json/writer.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

extern Json::FastWriter writer;
extern Json::Reader reader;
extern int ap_player_id;
extern int ap_player_team;
extern std::set<int> teams_set;
extern std::map<int, std::string> map_player_id_alias;

void AP_SetGiftBoxProperties(AP_GiftBoxProperties props) {
    // Create Local Box if needed
    AP_SetServerDataRequest req_local_box;
    req_local_box.key = "GiftBox;" + std::to_string(ap_player_team) + ";" + std::to_string(ap_player_id);
    std::string LocalGiftBoxDef_s = writer.write(Json::objectValue);
    req_local_box.operations = {{"replace", &LocalGiftBoxDef_s}};
    req_local_box.type = AP_DataType::Raw;
    req_local_box.want_reply = false;
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
    req_global_box.key = "GiftBox;" + std::to_string(ap_player_team);
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
        team_reqs[team].key = "GiftBox;" + std::to_string(team);
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
    return res;
}