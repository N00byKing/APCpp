# APCpp
C++ Library for Clients interfacing with the [Archipelago Multi-Game Randomizer](https://archipelago.gg)

# Usage

## Initialization

Run one of the `AP_Init` functions as the first call to the library:
- `AP_Init(const char*, const char*, const char*, const char*)` with IP, Game Name, Slot Name and password (can be `""`)
- `AP_Init(const char*)` with the filename corresponding to a generated single player game.

Then, you must call the following functions (any order):
- `AP_SetItemClearCallback(void (*f_itemclr)())` with a callback that clears all item states
- `AP_SetItemRecvCallback(void (*f_itemrecv)(int,bool))` with a callback that adds the item with the given ID (first parameter) to the current state.
The second parameter decides whether or not to notify the player
- `AP_SetLocationCheckedCallback(void (*f_locrecv)(int))` with a callback that marks the location with the given id (first parameter) as checked.

Optionally, for finer configuration:
- `AP_EnableQueueItemRecvMsgs(bool)` Enables or disables Item Messages for Items received for the current game. Alternative to using the game's native item reception handler, if present. Defaults to on.

Optionally, for DeathLink:
- `AP_SetDeathLinkSupported(bool)` Enables or disables DeathLink from the Library. Defaults to off. NOTE: If on, expects DeathLink in slotdata from Archipelago.
- `AP_SetDeathLinkRecvCallback(void (*f_deathrecv)())` Alternative to manual query. Optional callback to handle DeathLink.

Optionally, if slot data is required:
- `AP_RegisterSlotDataIntCallback(std::string, void (*f_slotdata)(int))` Add a callback that receives an int from slot data with the first parameter as its key.
- `AP_RegisterSlotDataMapIntIntCallback(std::string, void (*f_slotdata)(std::map<int,int>))` Add a callback that receives an int to int map from slot data with the first parameter as its key.

Optionally, for Gifting:
- `AP_AP_SetGiftingSupported(bool)` Enables or disables Gifting from the Library. Defaults to off. NOTE: needs to be on before other gifting methods can be called.
- `void AP_UseGiftAutoReject(bool);` Enables or disables automatic rejection of gifts when your giftbox is closed or the gifts doesnt match a trait with your desired traits.

Finally, call `AP_Start()`

## Operation during runtime

When the player completes a check, call `AP_SendItem(int)` with the Location ID as the parameter.
When the player completes the game, call `AP_StoryComplete`.

### Messages
Messages can be received from the Archipelago Server and this Library, such as Messages describing which item was sent, who was responsible for a Death received with DeathLink, etc.
To receive messages:
- Check if message is available using `AP_IsMessagePending()`
- Receive with `AP_GetLatestMessage()`. This returns an AP_Message struct with a type and preconfigured presentable text for this message. If you want the game client to have more details for the message (for example to create a custom text) any non-plaintext type message can be casted to an AP_`TYPE`Message with some type-specific struct members providing additional information for the given message.
- Clear the latest message with `AP_ClearLatestMessage()`.

### DeathLink
If DeathLink is supported, you have multiple ways of using it:
- Regularly call `AP_DeathLinkPending()` and check the return value. If true, kill the player and call `AP_DeathLinkClear()` (Preferably after a short time.
Faulty clients can send multiple deaths in quick succession. Ex.: Clear only after player has recovered from death).
- Handle death using a DeathLink callback (Registration described in [Initialization](#Initialization))

### Gifting
If Gifting is supported, you can interact with it using the following methods:
- `AP_RequestStatus AP_SetGiftBoxProperties(AP_GiftBoxProperties props);` Sets whether you want to receive gifts and what type of gifts, you can call this once and forget about it, or update your desires as you see fit.
- Regularly call `std::vector<AP_Gift> AP_CheckGifts();` to see what gifts are send to you.
- Then call `AP_RequestStatus AP_AcceptGift(std::string id);` or `AP_RequestStatus AP_RejectGift(std::string id);` to either accept or reject the gift, if the gift is accepted you award it to your player, if its rejected will be send back to the sender.
- When you want to send a gift, call `std::map<std::pair<int,std::string>,AP_GiftBoxProperties> AP_QueryGiftBoxes();` to find out what type of gifts other players desire, and then call `AP_RequestStatus AP_SendGift(AP_Gift gift);` to send a gift to who you have chosen.

# Building
Clone the Repo recursively!
## Linux
- Create a folder `build`
- `cd build`
- `cmake ..`
- `cmake --build .`
## Windows
- Create a folder `build`
- Enter the folder
- `cmake .. -DWIN32=1` (If on MinGW, also add `-DMINGW=1`. If `zlib` is not installed add `-DUSE_ZLIB=OFF`)
- `cmake --build .`
