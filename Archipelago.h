#pragma once

#include <deque>
#include <string>
#include <vector>

void AP_Init(const char*, const char*, const char*, const char*);
bool AP_IsInit();

void AP_SetDeathLinkSupported(bool);

void AP_SetItemClearCallback(void (*f_itemclr)());
void AP_SetItemRecvCallback(void (*f_itemrecv)(int));
void AP_SetLocationCheckedCallback(void (*f_locrecv)(int));
void AP_SetDeathLinkRecvCallback(void (*f_deathrecv)());
void AP_Start();

// Sends LocationCheck for given index
void AP_SendItem(int);

bool AP_DeathLinkPending();
void AP_DeathLinkClear();
void AP_DeathLinkSend();

// Called when Story completed, sends StatusUpdate
void AP_StoryComplete();

bool AP_IsMessagePending();
void AP_ClearLatestMessage();
std::vector<std::string> AP_GetLatestMessage();