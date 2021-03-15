#include <StdInc.h>
#include <CoreConsole.h>
#include <jitasm.h>
#include <Hooking.h>
#include <ScriptEngine.h>

#include <netBlender.h>
#include <netInterface.h>
#include <netObject.h>
#include <netObjectMgr.h>
#include <netSyncTree.h>
#include <rlNetBuffer.h>

#include <CloneManager.h>

#include <CrossBuildRuntime.h>
#include <ICoreGameInit.h>

#include <base64.h>

#include <NetLibrary.h>
#include <NetBuffer.h>

#include <Pool.h>

#include <EntitySystem.h>
#include <sysAllocator.h>

#include <ResourceManager.h>
#include <StateBagComponent.h>

#include <Error.h>

extern NetLibrary* g_netLibrary;

class CNetGamePlayer;

ICoreGameInit* icgi;

namespace rage
{
class netObject;

class netPlayerMgrBase
{
public:
	char pad[232 - 8];
	CNetGamePlayer* localPlayer;

public:
	virtual ~netPlayerMgrBase() = 0;

	virtual void Initialize() = 0;

	virtual void Shutdown() = 0;

	virtual void m_18() = 0;

#ifdef GTA_FIVE
	virtual CNetGamePlayer* AddPlayer(void* scInAddr, void* unkNetValue, void* addedIn1290, void* playerData, void* nonPhysicalPlayerData) = 0;
#elif IS_RDR3
	virtual CNetGamePlayer* AddPlayer(void* scInAddr, uint32_t activePlayerIndex, void* playerData, void* playerAccountId) = 0;
#endif

	virtual void RemovePlayer(CNetGamePlayer* player) = 0;

	void UpdatePlayerListsForPlayer(CNetGamePlayer* player);
};

static hook::thiscall_stub<void(netPlayerMgrBase*, CNetGamePlayer*)> _netPlayerMgrBase_UpdatePlayerListsForPlayer([]
{
#ifdef GTA_FIVE
	return hook::get_call(hook::get_pattern("FF 57 30 48 8B D6 49 8B CE E8", 9));
#elif IS_RDR3
	return hook::get_call(hook::get_pattern("41 B9 FF 00 00 00 4D 8B C5", 21));
#endif
});

void netPlayerMgrBase::UpdatePlayerListsForPlayer(CNetGamePlayer* player)
{
	_netPlayerMgrBase_UpdatePlayerListsForPlayer(this, player);
}
}

static rage::netPlayerMgrBase* g_playerMgr;

void* g_tempRemotePlayer;

CNetGamePlayer* g_players[256];
std::unordered_map<uint16_t, CNetGamePlayer*> g_playersByNetId;
std::unordered_map<CNetGamePlayer*, uint16_t> g_netIdsByPlayer;

static CNetGamePlayer* g_playerList[256];
static int g_playerListCount;

static CNetGamePlayer* g_playerListRemote[256];
static int g_playerListCountRemote;

static std::unordered_map<uint16_t, std::shared_ptr<fx::StateBag>> g_playerBags;

static CNetGamePlayer*(*g_origGetPlayerByIndex)(uint8_t);

static CNetGamePlayer* __fastcall GetPlayerByIndex(uint8_t index)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetPlayerByIndex(index);
	}

	// temp hack
	/*if (index == 0)
	{
		return g_playerMgr->localPlayer;
	}*/

	if (index < 0 || index >= 256)
	{
		return nullptr;
	}

	return g_players[index];
}

#ifdef GTA_FIVE
static void(*g_origJoinBubble)(void* bubbleMgr, CNetGamePlayer* player);

static void JoinPhysicalPlayerOnHost(void* bubbleMgr, CNetGamePlayer* player)
{
	g_origJoinBubble(bubbleMgr, player);

	if (!icgi->OneSyncEnabled)
	{
		return;
	}

	if (player != g_playerMgr->localPlayer)
	{
		return;
	}

	console::DPrintf("onesync", "Assigning physical player index for the local player.\n");

	auto clientId = g_netLibrary->GetServerNetID();
	auto idx = g_netLibrary->GetServerSlotID();

	player->physicalPlayerIndex() = idx;

	g_players[idx] = player;

	g_playersByNetId[clientId] = player;
	g_netIdsByPlayer[player] = clientId;

	// add to sequential list
	g_playerList[g_playerListCount] = player;
	g_playerListCount++;

	g_playerBags[clientId] = Instance<fx::ResourceManager>::Get()
							 ->GetComponent<fx::StateBagComponent>()
							 ->RegisterStateBag(fmt::sprintf("player:%d", clientId));

	// don't add to g_playerListRemote(!)
}
#elif IS_RDR3
static void*(*g_origJoinBubble)(void* bubbleMgr, void* scSessionImpl, void* playerDataMsg, void* a4, uint8_t slotIndex);

static void* JoinPhysicalPlayerOnHost(void* bubbleMgr, void* scSessionImpl, void* playerDataMsg, void* a4, uint8_t slotIndex)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origJoinBubble(bubbleMgr, scSessionImpl, playerDataMsg, a4, slotIndex);
	}

	console::DPrintf("onesync", "Assigning physical player index for the local player.\n");

	auto clientId = g_netLibrary->GetServerNetID();
	auto idx = g_netLibrary->GetServerSlotID();

	auto result = g_origJoinBubble(bubbleMgr, scSessionImpl, playerDataMsg, a4, idx);

	auto player = g_playerMgr->localPlayer;
	player->physicalPlayerIndex() = idx;

	g_players[idx] = player;

	g_playersByNetId[clientId] = player;
	g_netIdsByPlayer[player] = clientId;

	// add to sequential list
	g_playerList[g_playerListCount] = player;
	g_playerListCount++;

	g_playerBags[clientId] = Instance<fx::ResourceManager>::Get()
		->GetComponent<fx::StateBagComponent>()
		->RegisterStateBag(fmt::sprintf("player:%d", clientId));

	// don't add to g_playerListRemote(!)

	return result;
}
#endif

CNetGamePlayer* GetPlayerByNetId(uint16_t netId)
{
	return g_playersByNetId[netId];
}

CNetGamePlayer* GetLocalPlayer()
{
	return g_playerMgr->localPlayer;
}

static CNetGamePlayer*(*g_origGetPlayerByIndexNet)(int);

static CNetGamePlayer* GetPlayerByIndexNet(int index)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetPlayerByIndexNet(index);
	}

	// todo: check network game flag
	return GetPlayerByIndex(index);
}

static bool (*g_origNetPlayer_IsActive)(CNetGamePlayer*);

static bool netPlayer_IsActiveStub(CNetGamePlayer* player)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origNetPlayer_IsActive(player);
	}

	return true;
}

static bool(*g_origIsNetworkPlayerActive)(int);

static bool IsNetworkPlayerActive(int index)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origIsNetworkPlayerActive(index);
	}

	return (GetPlayerByIndex(index) != nullptr);
}

static bool(*g_origIsNetworkPlayerConnected)(int);

static bool IsNetworkPlayerConnected(int index)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origIsNetworkPlayerConnected(index);
	}

	return (GetPlayerByIndex(index) != nullptr);
}

#ifdef GTA_FIVE
int g_physIdx = 42;
#elif IS_RDR3
int g_physIdx = 1;
#endif

#ifdef GTA_FIVE
static hook::cdecl_stub<void(void*)> _npCtor([]()
{
	return hook::get_pattern("C7 41 08 0A 00 00 00 C7 41 0C 20 00 00 00", -0xA);
});
#endif

static hook::cdecl_stub<void(void*)> _pCtor([]()
{
#ifdef GTA_FIVE
	return hook::get_pattern("48 89 03 48 89 73 10 89 73 1C E8", -0x23);
#elif IS_RDR3
	return hook::get_pattern("83 4B 1C FF 48 8D 05 ? ? ? ? 33 C9 48", -0xE);
#endif
});

#ifdef IS_RDR3
static hook::cdecl_stub<void(uint8_t)> _initUnkPlayerArray([]()
{
	return hook::get_pattern("80 F9 20 73 ? 53 48 83 EC 20 8A D9");
});
#endif

namespace sync
{
#ifdef GTA_FIVE
	template<int Build>
	void TempHackMakePhysicalPlayerImpl(uint16_t clientId, int idx = -1)
	{
		auto npPool = rage::GetPoolBase("CNonPhysicalPlayerData");

		// probably shutting down the network subsystem
		if (!npPool)
		{
			return;
		}

		void* fakeInAddr = calloc(256, 1);
		void* fakeFakeData = calloc(256, 1);

		rlGamerInfo<Build>* inAddr = (rlGamerInfo<Build>*)fakeInAddr;
		inAddr->peerAddress.localAddr.ip.addr = clientId ^ 0xFEED;
		inAddr->peerAddress.relayAddr.ip.addr = clientId ^ 0xFEED;
		inAddr->peerAddress.publicAddr.ip.addr = clientId ^ 0xFEED;
		inAddr->peerAddress.rockstarAccountId = clientId;
		inAddr->gamerId = clientId;

		// this has to come from the pool directly as the game will expect to free it
		void* nonPhys = rage::PoolAllocate(npPool);
		_npCtor(nonPhys); // ctor

		void* phys = calloc(1024, 1);
		_pCtor(phys);

		auto player = g_playerMgr->AddPlayer(fakeInAddr, fakeFakeData, nullptr, phys, nonPhys);
		g_tempRemotePlayer = player;

		// so we can safely remove (do this *before* assigning physical player index, or the game will add
		// to a lot of lists which aren't >32-safe)
		//g_playerMgr->UpdatePlayerListsForPlayer(player);
		// NOTE: THIS IS NOT SAFE unless array manager/etc. are patched

		if (idx == -1)
		{
			idx = g_physIdx;
			g_physIdx++;
		}

		player->physicalPlayerIndex() = idx;
		g_players[idx] = player;

		g_playersByNetId[clientId] = player;
		g_netIdsByPlayer[player] = clientId;

		g_playerList[g_playerListCount] = player;
		g_playerListCount++;

		g_playerListRemote[g_playerListCountRemote] = player;
		g_playerListCountRemote++;

		// resort player lists
		std::sort(g_playerList, g_playerList + g_playerListCount, [](CNetGamePlayer * left, CNetGamePlayer * right)
		{
			return (left->physicalPlayerIndex() < right->physicalPlayerIndex());
		});

		std::sort(g_playerListRemote, g_playerListRemote + g_playerListCountRemote, [](CNetGamePlayer* left, CNetGamePlayer* right)
		{
			return (left->physicalPlayerIndex() < right->physicalPlayerIndex());
		});

		g_playerBags[clientId] = Instance<fx::ResourceManager>::Get()
			->GetComponent<fx::StateBagComponent>()
			->RegisterStateBag(fmt::sprintf("player:%d", clientId));
	}

	void TempHackMakePhysicalPlayer(uint16_t clientId, int idx = -1)
	{
		if (xbr::IsGameBuildOrGreater<2060>())
		{
			TempHackMakePhysicalPlayerImpl<2060>(clientId, idx);
		}
		else
		{
			TempHackMakePhysicalPlayerImpl<1604>(clientId, idx);
		}
	}
#elif IS_RDR3
	void TempHackMakePhysicalPlayer(uint16_t clientId, int idx = -1)
	{
		void* fakeInAddr = calloc(256, 1);
		void* fakeFakeData = calloc(256, 1);

		rlGamerInfo* inAddr = (rlGamerInfo*)fakeInAddr;
		inAddr->peerAddress.localAddr.ip.addr = (clientId ^ 0xFEED) | 0xc0a80000;
		inAddr->peerAddress.relayAddr.ip.addr = clientId ^ 0xFEED;
		inAddr->peerAddress.publicAddr.ip.addr = clientId ^ 0xFEED;
		inAddr->peerAddress.rockstarAccountId = clientId;

		void* phys = calloc(1024, 1);
		_pCtor(phys);

		auto player = g_playerMgr->AddPlayer(fakeInAddr, 0, phys, &fakeFakeData);
		g_tempRemotePlayer = player;

		if (idx == -1)
		{
			idx = g_physIdx;
			g_physIdx++;
		}

		_initUnkPlayerArray(idx);

		player->physicalPlayerIndex() = idx;
		g_players[idx] = player;

		g_playersByNetId[clientId] = player;
		g_netIdsByPlayer[player] = clientId;

		g_playerList[g_playerListCount] = player;
		g_playerListCount++;

		g_playerListRemote[g_playerListCountRemote] = player;
		g_playerListCountRemote++;

		// resort player lists
		std::sort(g_playerList, g_playerList + g_playerListCount, [](CNetGamePlayer* left, CNetGamePlayer* right)
		{
			return (left->physicalPlayerIndex() < right->physicalPlayerIndex());
		});

		std::sort(g_playerListRemote, g_playerListRemote + g_playerListCountRemote, [](CNetGamePlayer* left, CNetGamePlayer* right)
		{
			return (left->physicalPlayerIndex() < right->physicalPlayerIndex());
		});

		g_playerBags[clientId] = Instance<fx::ResourceManager>::Get()
			->GetComponent<fx::StateBagComponent>()
			->RegisterStateBag(fmt::sprintf("player:%d", clientId));
	}
#endif
}

void HandleClientInfo(const NetLibraryClientInfo& info)
{
	if (info.netId != g_netLibrary->GetServerNetID())
	{
		console::DPrintf("onesync", "Creating physical player %d (%s)\n", info.slotId, info.name);

		sync::TempHackMakePhysicalPlayer(info.netId, info.slotId);
	}
}

static hook::cdecl_stub<void* (CNetGamePlayer*)> getPlayerPedForNetPlayer([]()
{
#ifdef GTA_FIVE
	return hook::get_call(hook::get_pattern("84 C0 74 1C 48 8B CF E8 ? ? ? ? 48 8B D8", 7));
#elif IS_RDR3
	return hook::get_call(hook::get_pattern("48 8B CD 0F 11 06 48 8B D8 E8", -8));
#endif
});

#ifdef GTA_FIVE
static const uint32_t g_entityNetObjOffset = 208;
#elif IS_RDR3
static const uint32_t g_entityNetObjOffset = 224;
#endif

rage::netObject* GetLocalPlayerPedNetObject()
{
	auto ped = getPlayerPedForNetPlayer(g_playerMgr->localPlayer);

	if (ped)
	{
		auto netObj = *(rage::netObject**)((char*)ped + g_entityNetObjOffset);

		return netObj;
	}

	return nullptr;
}

void HandleClientDrop(const NetLibraryClientInfo& info)
{
	if (info.netId != g_netLibrary->GetServerNetID() && info.slotId != g_netLibrary->GetServerSlotID())
	{
		console::DPrintf("onesync", "Processing removal for player %d (%s)\n", info.slotId, info.name);

		if (info.slotId < 0 || info.slotId >= _countof(g_players))
		{
			console::DPrintf("onesync", "That's not a valid slot! Aborting.\n");
			return;
		}

		auto player = g_players[info.slotId];

		if (!player)
		{
			console::DPrintf("onesync", "That slot has no player. Aborting.\n");
			return;
		}

		// reassign the player's ped
		// TODO: only do this on a single client(!)

		// 1604 unused
		uint16_t objectId = 0;
		//auto ped = ((void*(*)(void*, uint16_t*, CNetGamePlayer*))hook::get_adjusted(0x141022B20))(nullptr, &objectId, player);
		auto ped = getPlayerPedForNetPlayer(player);

		// reset player (but not until we have gotten the ped)
		player->Reset();

		if (ped)
		{
			auto netObj = *(rage::netObject**)((char*)ped + g_entityNetObjOffset);

			if (netObj)
			{
				objectId = netObj->GetObjectId();
			}
		}

		console::DPrintf("onesync", "reassigning ped: %016llx %d\n", (uintptr_t)ped, objectId);

		if (ped)
		{
			// prevent stack overflow
			if (!info.name.empty())
			{
				TheClones->DeleteObjectId(objectId, 0, true);
			}

			console::DPrintf("onesync", "deleted object id\n");

			// 1604 unused
			//((void(*)(void*, uint16_t, CNetGamePlayer*))hook::get_adjusted(0x141008D14))(ped, objectId, player);

			console::DPrintf("onesync", "success! reassigned the ped!\n");
		}

		// make non-physical so we will remove from the non-physical list only
		/*auto physIdx = player->physicalPlayerIndex();
		player->physicalPlayerIndex() = -1;

		// remove object manager pointer temporarily
		static auto objectMgr = hook::get_address<void**>(hook::get_pattern("48 8B FA 0F B7 51 30 48 8B 0D ? ? ? ? 45 33 C0", 10));
		auto ogMgr = *objectMgr;
		*objectMgr = nullptr;

		// remove
		g_playerMgr->RemovePlayer(player);

		// restore
		*objectMgr = ogMgr;
		player->physicalPlayerIndex() = physIdx;*/
		// ^ is NOT safe, array handler manager etc. have 32 limits

		// TEMP: properly handle order so that we don't have to fake out the game
		g_playersByNetId[info.netId] = nullptr;
		g_netIdsByPlayer[player] = -1;

		for (int i = 0; i < g_playerListCount; i++)
		{
			if (g_playerList[i] == player)
			{
				memmove(&g_playerList[i], &g_playerList[i + 1], sizeof(*g_playerList) * (g_playerListCount - i - 1));
				g_playerListCount--;

				break;
			}
		}

		for (int i = 0; i < g_playerListCountRemote; i++)
		{
			if (g_playerListRemote[i] == player)
			{
				memmove(&g_playerListRemote[i], &g_playerListRemote[i + 1], sizeof(*g_playerListRemote) * (g_playerListCountRemote - i - 1));
				g_playerListCountRemote--;

				break;
			}
		}

		// resort player lists
		std::sort(g_playerList, g_playerList + g_playerListCount, [](CNetGamePlayer * left, CNetGamePlayer * right)
		{
			return (left->physicalPlayerIndex() < right->physicalPlayerIndex());
		});

		std::sort(g_playerListRemote, g_playerListRemote + g_playerListCountRemote, [](CNetGamePlayer* left, CNetGamePlayer* right)
		{
			return (left->physicalPlayerIndex() < right->physicalPlayerIndex());
		});

		g_players[info.slotId] = nullptr;
		g_playerBags.erase(info.netId);
	}
}

static CNetGamePlayer*(*g_origGetOwnerNetPlayer)(rage::netObject*);

CNetGamePlayer* netObject__GetPlayerOwner(rage::netObject* object)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetOwnerNetPlayer(object);
	}

	if (object && object->syncData.ownerId == 31)
	{
		auto player = g_playersByNetId[TheClones->GetClientId(object)];

		// FIXME: figure out why bad playerinfos occur
		if (player != nullptr && player->GetPlayerInfo() != nullptr)
		{
			return player;
		}
	}

	return g_playerMgr->localPlayer;
}

static uint8_t(*g_origGetOwnerPlayerId)(rage::netObject*);

static uint8_t netObject__GetPlayerOwnerId(rage::netObject* object)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetOwnerPlayerId(object);
	}

	return netObject__GetPlayerOwner(object)->physicalPlayerIndex();
}

static CNetGamePlayer*(*g_origGetPendingPlayerOwner)(rage::netObject*);

static CNetGamePlayer* netObject__GetPendingPlayerOwner(rage::netObject* object)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetPendingPlayerOwner(object);
	}

	if (object->syncData.nextOwnerId != 0xFF)
	{
		auto player = g_playersByNetId[TheClones->GetPendingClientId(object)];

		if (player != nullptr && player->GetPlayerInfo() != nullptr)
		{
			return player;
		}
	}

	return nullptr;
}

static hook::cdecl_stub<uint32_t(void*)> getScriptGuidForEntity([]()
{
#ifdef GTA_FIVE
	return hook::get_pattern("48 F7 F9 49 8B 48 08 48 63 D0 C1 E0 08 0F B6 1C 11 03 D8", -0x68);
#elif IS_RDR3
	return hook::get_pattern("32 DB E8 ? ? ? ? 48 85 C0 75 ? 8A 05", -35);
#endif
});

#ifdef GTA_FIVE
struct ReturnedCallStub : public jitasm::Frontend
{
	ReturnedCallStub(int idx, uintptr_t targetFunc)
		: m_index(idx), m_targetFunc(targetFunc)
	{

	}

	static void InstrumentedTarget(void* targetFn, void* callFn, int index)
	{
		console::DPrintf("onesync", "called %016llx (m_%x) from %016llx\n", (uintptr_t)targetFn, index, (uintptr_t)callFn);
	}

	virtual void InternalMain() override
	{
		push(rcx);
		push(rdx);
		push(r8);
		push(r9);

		mov(rcx, m_targetFunc);
		mov(rdx, qword_ptr[rsp + 0x20]);
		mov(r8d, m_index);

		// scratch space (+ alignment for stack)
		sub(rsp, 0x28);

		mov(rax, (uintptr_t)InstrumentedTarget);
		call(rax);

		add(rsp, 0x28);

		pop(r9);
		pop(r8);
		pop(rdx);
		pop(rcx);

		mov(rax, m_targetFunc);
		jmp(rax);
	}

private:
	uintptr_t m_originFunc;
	uintptr_t m_targetFunc;

	int m_index;
};

static void NetLogStub_DoTrace(void*, const char* fmt, ...)
{
	char buffer[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, ap);
	va_end(ap);

	console::DPrintf("onesync", "[NET] %s\n", buffer);
}

static void NetLogStub_DoLog(void*, const char* type, const char* fmt, ...)
{
	char buffer[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, ap);
	va_end(ap);

	console::DPrintf("onesync", "[NET] %s %s\n", type, buffer);
}
#endif

static hook::cdecl_stub<CNetGamePlayer*(void*)> _netPlayerCtor([]()
{
#ifdef GTA_FIVE
	return hook::get_pattern("83 8B ? 00 00 00 FF 33 F6", -0x17);
#elif IS_RDR3
	return hook::get_pattern("E8 ? ? ? ? 33 F6 48 8D 05 ? ? ? ? 48 8D 8B", -0x17);
#endif
});

static CNetGamePlayer*(*g_origAllocateNetPlayer)(void*);

static CNetGamePlayer* AllocateNetPlayer(void* mgr)
{
	// REDM1S: crashes if you try to allocate fake player for local player
	if (!icgi->OneSyncEnabled
#ifdef IS_RDR3
		|| mgr != nullptr
#endif
	)
	{
		return g_origAllocateNetPlayer(mgr);
	}

#ifdef GTA_FIVE
	void* plr = malloc(xbr::IsGameBuildOrGreater<2060>() ? 688 : 672);
#elif IS_RDR3
	void* plr = malloc(2784); // for 1207: 2640
#endif

	return _netPlayerCtor(plr);
}

#include <minhook.h>

static void(*g_origPassObjectControl)(CNetGamePlayer* player, rage::netObject* netObject, int a3);

void ObjectIds_RemoveObjectId(int objectId);

static void PassObjectControlStub(CNetGamePlayer* player, rage::netObject* netObject, int a3)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origPassObjectControl(player, netObject, a3);
	}

	if (player->physicalPlayerIndex() == netObject__GetPlayerOwner(netObject)->physicalPlayerIndex())
	{
		return;
	}

	console::DPrintf("onesync", "passing object %016llx (%d) control from %d to %d\n", (uintptr_t)netObject, netObject->GetObjectId(), netObject->syncData.ownerId, player->physicalPlayerIndex());
	TheClones->Log("%s: passing object %016llx (%d) control from %d to %d\n", __func__, (uintptr_t)netObject, netObject->GetObjectId(), netObject->syncData.ownerId, player->physicalPlayerIndex());

	ObjectIds_RemoveObjectId(netObject->GetObjectId());

	netObject->syncData.nextOwnerId = 31;
	TheClones->SetTargetOwner(netObject, g_netIdsByPlayer[player]);

	// REDM1S: implement
#ifdef GTA_FIVE
	fwEntity* entity = (fwEntity*)netObject->GetGameObject();
	if (entity && entity->IsOfType(HashString("CVehicle")))
	{
		CVehicle* vehicle = static_cast<CVehicle*>(entity);
		VehicleSeatManager* seatManager = vehicle->GetSeatManager();

		for (int i = 0; i < seatManager->GetNumSeats(); i++)
		{
			auto occupant = seatManager->GetOccupant(i);

			if (occupant)
			{
				auto netOccupant = reinterpret_cast<rage::netObject*>(occupant->GetNetObject());

				if (netOccupant)
				{
					if (!netOccupant->syncData.isRemote && netOccupant->GetObjectType() != 11)
					{
						console::DPrintf("onesync", "passing occupant %d control as well\n", netOccupant->GetObjectId());

						PassObjectControlStub(player, netOccupant, a3);
					}
				}
			}
		}
	}
#endif

	//auto lastIndex = player->physicalPlayerIndex();
	//player->physicalPlayerIndex() = 31;

	g_origPassObjectControl(player, netObject, a3);

	//player->physicalPlayerIndex() = lastIndex;
}

static void(*g_origSetOwner)(rage::netObject* object, CNetGamePlayer* newOwner);

static void SetOwnerStub(rage::netObject* netObject, CNetGamePlayer* newOwner)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origSetOwner(netObject, newOwner);
	}

	if (newOwner->physicalPlayerIndex() == g_playerMgr->localPlayer->physicalPlayerIndex())
	{
		TheClones->Log("%s: taking ownership of object id %d - stack trace:\n", __func__, netObject->GetObjectId());

		uintptr_t* traceStart = (uintptr_t*)_AddressOfReturnAddress();

		for (int i = 0; i < 96; i++)
		{
			if ((i % 6) == 0)
			{
				TheClones->Log("\n");
			}

			TheClones->Log("%016llx ", traceStart[i]);
		}

		TheClones->Log("\n");
	}

	g_origSetOwner(netObject, newOwner);

	if (newOwner->physicalPlayerIndex() == netObject__GetPlayerOwner(netObject)->physicalPlayerIndex())
	{
		return;
	}

	TheClones->Log("%s: passing object %016llx (%d) ownership from %d to %d\n", __func__, (uintptr_t)netObject, netObject->GetObjectId(), netObject->syncData.ownerId, newOwner->physicalPlayerIndex());

	ObjectIds_RemoveObjectId(netObject->GetObjectId());

	netObject->syncData.nextOwnerId = 31;
	TheClones->SetTargetOwner(netObject, g_netIdsByPlayer[newOwner]);
}

static bool m158Stub(rage::netObject* object, CNetGamePlayer* player, int type, int* outReason)
{
	int reason;
	bool rv = object->m_158(player, type, &reason);

	if (!rv)
	{
		console::DPrintf("onesync", "couldn't pass control for reason %d\n", reason);
	}

	return rv;
}

#ifdef GTA_FIVE
static bool m168Stub(rage::netObject* object, int* outReason)
{
	int reason;
	bool rv = object->m_168(&reason);

	if (!rv)
	{
		//trace("couldn't blend object for reason %d\n", reason);
	}

	return rv;
}
#endif

int getPlayerId()
{
	return g_playerMgr->localPlayer->physicalPlayerIndex();
}

static bool mD0Stub(rage::netSyncTree* tree, int a2)
{
	if (icgi->OneSyncEnabled)
	{
		return false;
	}

#ifdef GTA_FIVE
	return tree->m_D0(a2);
#elif IS_RDR3
	return tree->m_C0(a2);
#endif
}

static int(*g_origGetNetworkPlayerListCount)();
static CNetGamePlayer**(*g_origGetNetworkPlayerList)();

static int netInterface_GetNumRemotePhysicalPlayers()
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetNetworkPlayerListCount();
	}

	return g_playerListCountRemote;
}

static CNetGamePlayer** netInterface_GetRemotePhysicalPlayers()
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetNetworkPlayerList();
	}

	return g_playerListRemote;
}

static int(*g_origGetNetworkPlayerListCount2)();
static CNetGamePlayer**(*g_origGetNetworkPlayerList2)();

static int netInterface_GetNumPhysicalPlayers()
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetNetworkPlayerListCount2();
	}

	return g_playerListCount;
}

static CNetGamePlayer** netInterface_GetAllPhysicalPlayers()
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetNetworkPlayerList2();
	}

	return g_playerList;
}

static void netObject__ClearPendingPlayerIndex(rage::netObject* object)
{
	if (icgi->OneSyncEnabled)
	{
		if (object->syncData.nextOwnerId == 31 && TheClones->GetPendingClientId(object) != 0xFFFF)
		{
			return;
		}
	}

	object->syncData.nextOwnerId = -1;
}

#ifdef GTA_FIVE
static void*(*g_origGetScenarioTaskScenario)(void* scenarioTask);

static void* GetScenarioTaskScenario(char* scenarioTask)
{
	void* scenario = g_origGetScenarioTaskScenario(scenarioTask);

	if (!scenario)
	{
		static bool hasCrashedBefore = false;

		if (!hasCrashedBefore)
		{
			hasCrashedBefore = true;

			trace("WARNING: invalid scenario task triggered (scenario ID: %d)\n", *(uint32_t*)(scenarioTask + 168));
		}

		*(uint32_t*)(scenarioTask + 168) = 0;

		scenario = g_origGetScenarioTaskScenario(scenarioTask);

		assert(scenario);
	}

	return scenario;
}
#endif

static int(*g_origNetworkBandwidthMgr_CalculatePlayerUpdateLevels)(void* mgr, int* a2, int* a3, int* a4);

static int networkBandwidthMgr_CalculatePlayerUpdateLevelsStub(void* mgr, int* a2, int* a3, int* a4)
{
	if (icgi->OneSyncEnabled)
	{
		return 0;
	}

	return g_origNetworkBandwidthMgr_CalculatePlayerUpdateLevels(mgr, a2, a3, a4);
}

#ifdef GTA_FIVE
static int(*g_origGetNetObjPlayerGroup)(void* entity);

static int GetNetObjPlayerGroup(void* entity)
{
	// #TODO1S: groups
	// indexes a fixed-size array of 64, of which 32 are players, and 16+16 are script/code groups
	// we don't want to write past 32 ever, and _especially_ not past 64
	// this needs additional patching that currently isn't done.
	if (icgi->OneSyncEnabled)
	{
		return 0;
	}

	return g_origGetNetObjPlayerGroup(entity);
}
#endif

using TObjectPred = bool(*)(rage::netObject*);

static int(*g_origCountObjects)(rage::netObjectMgr* objectMgr, TObjectPred pred);

static int netObjectMgr__CountObjects(rage::netObjectMgr* objectMgr, TObjectPred pred)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origCountObjects(objectMgr, pred);
	}

	auto objectList = TheClones->GetObjectList();

	return std::count_if(objectList.begin(), objectList.end(), pred);
}

// REDM1S: haven't check if code valid for RDR3
static void(*g_origObjectManager_End)(void*);

void ObjectManager_End(rage::netObjectMgr* objectMgr)
{
	if (icgi->OneSyncEnabled)
	{
		// don't run deletion if object manager is already shut down
		char* mgrPtr = (char*)objectMgr;

		if (*(bool*)(mgrPtr + 9992))
		{
			auto objectCb = [objectMgr](rage::netObject* object)
			{
				if (!object)
				{
					return;
				}

				// don't force-delete the local player
				if (object->GetObjectType() == (uint16_t)NetObjEntityType::Player && !object->syncData.isRemote)
				{
					objectMgr->UnregisterNetworkObject(object, 0, true, false);
					return;
				}

				objectMgr->UnregisterNetworkObject(object, 0, true, true);
			};

			for (int i = 0; i < 256; i++)
			{
				CloneObjectMgr->ForAllNetObjects(i, objectCb, true);
			}

			auto listCopy = TheClones->GetObjectList();

			for (auto netObj : listCopy)
			{
				objectCb(netObj);
			}
		}
	}

	g_origObjectManager_End(objectMgr);
}

// REDM1S: haven't tested in RDR3
static void(*g_origPlayerManager_End)(void*);

void PlayerManager_End(void* mgr)
{
	if (icgi->OneSyncEnabled)
	{
		g_netIdsByPlayer.clear();
		g_playersByNetId.clear();

		for (auto& p : g_players)
		{
			if (p)
			{
				if (p != g_playerMgr->localPlayer)
				{
					console::DPrintf("onesync", "player manager shutdown: resetting player %s\n", p->GetName());
					p->Reset();
				}
			}

			p = nullptr;
		}

		// reset the player list so we won't try removing _any_ players
		// #TODO1S: don't corrupt the physical linked list in the first place
		*(void**)((char*)g_playerMgr + 288) = nullptr;
		*(void**)((char*)g_playerMgr + 296) = nullptr;
		*(uint32_t*)((char*)g_playerMgr + 304) = 0;

		*(void**)((char*)g_playerMgr + 312) = nullptr;
		*(void**)((char*)g_playerMgr + 320) = nullptr;
		*(uint32_t*)((char*)g_playerMgr + 328) = 0;

		g_playerListCount = 0;
		g_playerListCountRemote = 0;
	}

	g_origPlayerManager_End(mgr);
}

namespace rage
{
	struct rlGamerId
	{
		uint64_t accountId;
	};
}

static rage::netPlayer*(*g_origGetPlayerFromGamerId)(rage::netPlayerMgrBase* mgr, const rage::rlGamerId& gamerId, bool flag);

#ifdef GTA_FIVE
template<int Build>
static rage::netPlayer* GetPlayerFromGamerId(rage::netPlayerMgrBase* mgr, const rage::rlGamerId& gamerId, bool flag)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetPlayerFromGamerId(mgr, gamerId, flag);
	}

	for (auto p : g_players)
	{
		if (p)
		{
			if (p->GetGamerInfo<Build>()->peerAddress.rockstarAccountId == gamerId.accountId)
			{
				return p;
			}
		}
	}

	return nullptr;
}
#elif IS_RDR3
static rage::netPlayer* GetPlayerFromGamerId(rage::netPlayerMgrBase* mgr, const rage::rlGamerId& gamerId, bool flag)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetPlayerFromGamerId(mgr, gamerId, flag);
	}

	for (auto p : g_players)
	{
		if (p)
		{
			if (p->GetGamerInfo()->peerAddress.rockstarAccountId == gamerId.accountId)
			{
				return p;
			}
		}
	}

	return nullptr;
}
#endif

static float VectorDistance(const float* point1, const float* point2)
{
	float xd = point1[0] - point2[0];
	float yd = point1[1] - point2[1];
	float zd = point1[2] - point2[2];

	return sqrtf((xd * xd) + (yd * yd) + (zd * zd));
}

#if GTA_FIVE
static hook::cdecl_stub<float*(float*, CNetGamePlayer*, void*, bool)> getNetPlayerRelevancePosition([]()
{
	// 1737: Arxan.
	if (xbr::IsGameBuildOrGreater<2060>())
	{
		return hook::get_call(hook::get_pattern("48 8D 4C 24 40 45 33 C9 45 33 C0 48 8B D0 E8", 0xE));
	}
	else
	{
		return hook::get_pattern("45 33 FF 48 85 C0 0F 84 5B 01 00 00", -0x34);
	}
});
#elif IS_RDR3
static hook::cdecl_stub<float* (float*, CNetGamePlayer*, void*)> getNetPlayerRelevancePosition([]()
{
	return hook::get_pattern("44 0F A3 C0 0F 92 C0 41 88 02", -0x32);
});
#endif

#ifdef GTA_FIVE
static int(*g_origGetPlayersNearPoint)(const float* point, float range, CNetGamePlayer* outArray[32], bool sorted, bool unkVal);

static int GetPlayersNearPoint(const float* point, float range, CNetGamePlayer* outArray[32], bool sorted, bool unkVal)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetPlayersNearPoint(point, range, outArray, sorted, unkVal);
	}

	CNetGamePlayer* tempArray[512];

	int idx = 0;

	auto playerList = netInterface_GetRemotePhysicalPlayers();
	for (int i = 0; i < netInterface_GetNumRemotePhysicalPlayers(); i++)
	{
		auto player = playerList[i];

		if (getPlayerPedForNetPlayer(player))
		{
			alignas(16) float vectorPos[4];

			if (range >= 100000000.0f || VectorDistance(point, getNetPlayerRelevancePosition(vectorPos, player, nullptr, unkVal)) < range)
			{
				tempArray[idx] = player;
				idx++;
			}
		}
	}

	if (sorted)
	{
		std::sort(tempArray, tempArray + idx, [point](CNetGamePlayer* a1, CNetGamePlayer* a2)
		{
			alignas(16) float vectorPos1[4];
			alignas(16) float vectorPos2[4];

			float d1 = VectorDistance(point, getNetPlayerRelevancePosition(vectorPos1, a1, nullptr, false));
			float d2 = VectorDistance(point, getNetPlayerRelevancePosition(vectorPos2, a2, nullptr, false));

			return (d1 < d2);
		});
	}

	idx = std::min(idx, 32);

	std::copy(tempArray, tempArray + idx, outArray);

	return idx;
}
#elif IS_RDR3
static int(*g_origGetPlayersNearPoint)(const float* point, uint32_t unkIndex, void* outIndex, CNetGamePlayer* outArray[32], bool unkVal, float range, bool sorted);

static int GetPlayersNearPoint(const float* point, uint32_t unkIndex, void* outIndex, CNetGamePlayer* outArray[32], bool unkVal, float range, bool sorted)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetPlayersNearPoint(point, unkIndex, outIndex, outArray, range, range, sorted);
	}

	CNetGamePlayer* tempArray[512];

	int idx = 0;

	auto playerList = netInterface_GetRemotePhysicalPlayers();
	for (int i = 0; i < netInterface_GetNumRemotePhysicalPlayers(); i++)
	{
		auto player = playerList[i];

		if (getPlayerPedForNetPlayer(player))
		{
			alignas(16) float vectorPos[4];

			if (range >= 100000000.0f || VectorDistance(point, getNetPlayerRelevancePosition(vectorPos, player, nullptr)) < range)
			{
				tempArray[idx] = player;
				idx++;
			}
		}
	}

	if (sorted)
	{
		std::sort(tempArray, tempArray + idx, [point](CNetGamePlayer* a1, CNetGamePlayer* a2)
		{
			alignas(16) float vectorPos1[4];
			alignas(16) float vectorPos2[4];

			float d1 = VectorDistance(point, getNetPlayerRelevancePosition(vectorPos1, a1, nullptr));
			float d2 = VectorDistance(point, getNetPlayerRelevancePosition(vectorPos2, a2, nullptr));

			return (d1 < d2);
		});
	}

	idx = std::min(idx, 32);
	unkIndex = idx;

	std::copy(tempArray, tempArray + idx, outArray);

	return idx;
}
#endif

static void(*g_origVoiceChatMgr_EstimateBandwidth)(void*);

static void VoiceChatMgr_EstimateBandwidth(void* mgr)
{
	if (icgi->OneSyncEnabled)
	{
		return;
	}

	g_origVoiceChatMgr_EstimateBandwidth(mgr);
}

namespace sync
{
	extern net::Buffer g_cloneMsgPacket;
	extern std::vector<uint8_t> g_cloneMsgData;
}

#ifdef GTA_FIVE
static void(*g_origCPedGameStateDataNode__access)(void*, void*);

static void CPedGameStateDataNode__access(char* dataNode, void* accessor)
{
	g_origCPedGameStateDataNode__access(dataNode, accessor);

	// not needed right now
	return;

	// 1604 unused

	// if on mount/mount ID is set
	if (*(uint16_t*)(dataNode + 310) && icgi->OneSyncEnabled)
	{
		auto extraDumpPath = MakeRelativeCitPath(L"data\\cache\\extra_dump_info.bin");

		auto f = _wfopen(extraDumpPath.c_str(), L"wb");

		if (f)
		{
			fwrite(sync::g_cloneMsgPacket.GetData().data(), 1, sync::g_cloneMsgPacket.GetData().size(), f);
			fclose(f);
		}

		extraDumpPath = MakeRelativeCitPath(L"data\\cache\\extra_dump_info2.bin");

		f = _wfopen(extraDumpPath.c_str(), L"wb");

		if (f)
		{
			fwrite(sync::g_cloneMsgData.data(), 1, sync::g_cloneMsgData.size(), f);
			fclose(f);
		}

		FatalError("CPedGameStateDataNode: tried to read a mount ID, this is wrong, please click 'save information' below and post the file in https://forum.fivem.net/t/318260 to help us resolve this issue.");
	}
}
#endif

static void(*g_origManageHostMigration)(void*);

static void ManageHostMigrationStub(void* a1)
{
	if (!icgi->OneSyncEnabled)
	{
		g_origManageHostMigration(a1);
	}
}

static void(*g_origUnkBubbleWrap)();

static void UnkBubbleWrap()
{
	if (!icgi->OneSyncEnabled)
	{
		g_origUnkBubbleWrap();
	}
}

static void(*g_origUnkEventMgr)(void*, void*);

static void UnkEventMgr(void* mgr, void* ply)
{
	if (!icgi->OneSyncEnabled)
	{
		g_origUnkEventMgr(mgr, ply);
	}
}

#ifdef GTA_FIVE
static void* (*g_origNetworkObjectMgrCtor)(void*, void*);

static void* NetworkObjectMgrCtorStub(void* mgr, void* bw)
{
	auto alloc = rage::GetAllocator();
	alloc->Free(mgr);

	mgr = alloc->Allocate(32712 + 4096, 16, 0);

	g_origNetworkObjectMgrCtor(mgr, bw);

	return mgr;
}
#elif IS_RDR3
static void* (*g_origNetworkObjectMgrCtor)(void*, void*, void*);

static void* NetworkObjectMgrCtorStub(void* mgr, void* bw, void* unk)
{
	auto alloc = rage::GetAllocator();
	alloc->Free(mgr);

	int initialSize = (xbr::IsGameBuildOrGreater<1355>()) ? 268672 : 163056;

	mgr = alloc->Allocate(initialSize + 4096, 16, 0);

	g_origNetworkObjectMgrCtor(mgr, bw, unk);

	return mgr;
}
#endif

static HookFunction hookFunction([]()
{
#ifdef GTA_FIVE
	g_playerMgr = *hook::get_address<rage::netPlayerMgrBase**>(hook::get_pattern("40 80 FF 20 72 B3 48 8B 0D", 9));
#elif IS_RDR3
	g_playerMgr = *hook::get_address<rage::netPlayerMgrBase**>(hook::get_pattern("80 E1 07 80 F9 03 0F 84 ? ? ? ? 48 8B 0D", 15));
#endif

#ifdef GTA_FIVE
	// net damage array, size 32*4
	uint32_t* damageArrayReplacement = (uint32_t*)hook::AllocateStubMemory(256 * sizeof(uint32_t));
	memset(damageArrayReplacement, 0, 256 * sizeof(uint32_t));

	{
		std::initializer_list<std::tuple<std::string_view, int>> bits = {
			{ "74 30 3C 20 73 0D 48 8D 0D", 9 },
			{ "0F 85 9F 00 00 00 48 85 FF", 0x12 },
			{ "80 F9 FF 74 2F 48 8D 15", 8 },
			{ "80 BF ? 00 00 00 FF 74 21 48 8D", 12 }
		};

		for (const auto& bit : bits)
		{
			auto location = hook::get_pattern<int32_t>(std::get<0>(bit), std::get<1>(bit));

			*location = (intptr_t)damageArrayReplacement - (intptr_t)location - 4;
		}

		// 128
		hook::put<uint8_t>(hook::get_pattern("74 30 3C 20 73 0D 48 8D 0D", 3), 0x80);
	}

	// temp dbg
	//hook::put<uint16_t>(hook::get_pattern("0F 84 80 00 00 00 49 8B 07 49 8B CF FF 50 20"), 0xE990);

	//hook::put(0x141980A68, NetLogStub_DoLog);
	//hook::put(0x141980A50, NetLogStub_DoTrace);
	//hook::jump(0x140A19640, NetLogStub_DoLog);
#endif

	// netobjmgr count, temp dbg
#ifdef GTA_FIVE
	hook::put<uint8_t>(hook::get_pattern("48 8D 05 ? ? ? ? BE 1F 00 00 00 48 8B F9", 8), 128);
#elif IS_RDR3
	//hook::put<uint8_t>(hook::get_pattern("48 8D 05 ? ? ? ? BF 20 00 00 00 48 89 01", 8), 128);
#endif

	// RDR3 tasks sync hacks
#ifdef IS_RDR3
	hook::put<uint8_t>(hook::get_pattern("8B 0C 10 85 0A 75 ? 49 FF C0", 5), 0xEB);
#endif

	// 1604 unused, netobjmgr alloc size, temp dbg
	// 1737 made this arxan
	//hook::put<uint32_t>(0x14101CF4F, 32712 + 4096);

	MH_Initialize();

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("48 89 03 33 C0 B1 0A 48 89", -0x15), NetworkObjectMgrCtorStub, (void**)&g_origNetworkObjectMgrCtor);
	MH_CreateHook(hook::get_pattern("4C 8B F1 41 BD 05", -0x22), PassObjectControlStub, (void**)&g_origPassObjectControl);
	MH_CreateHook(hook::get_pattern("8A 41 49 4C 8B F2 48 8B", -0x10), SetOwnerStub, (void**)&g_origSetOwner);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("48 8B D9 E8 ? ? ? ? 33 ? 66 C7 83", xbr::IsGameBuildOrGreater<1355>() ? -0xA : -0x6), NetworkObjectMgrCtorStub, (void**)&g_origNetworkObjectMgrCtor);
	MH_CreateHook(hook::get_pattern("83 FE 01 41 0F 9F C4 48 85 DB 74", -0x71), PassObjectControlStub, (void**)&g_origPassObjectControl);
	MH_CreateHook(hook::get_pattern("80 79 ? ? 48 8B F2 48 8B F9 73", -0xF), SetOwnerStub, (void**)&g_origSetOwner);
#endif

	// scriptHandlerMgr::ManageHostMigration, has fixed 32 player array and isn't needed* for 1s
#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("01 4F 60 81 7F 60 D0 07 00 00 0F 8E", -0x47), ManageHostMigrationStub, (void**)&g_origManageHostMigration);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("83 F8 01 0F 86 ? ? ? ? 8B 0D ? ? ? ? 01 4F", -0x38), ManageHostMigrationStub, (void**)&g_origManageHostMigration);
#endif

	// 1604 unused, some bubble stuff
	//hook::return_function(0x14104D148);
#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("33 F6 33 DB 33 ED 0F 28 80", -0x3A), UnkBubbleWrap, (void**)&g_origUnkBubbleWrap);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("48 83 EC ? 8A 05 ? ? ? ? 33 DB 0F 29 74 24 60", -6), UnkBubbleWrap, (void**)&g_origUnkBubbleWrap);
#endif

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("0F 29 70 C8 0F 28 F1 33 DB 45", -0x1C), GetPlayersNearPoint, (void**)&g_origGetPlayersNearPoint);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("33 DB 0F 29 70 D8 49 8B F9 4D 8B F0", -0x1B), GetPlayersNearPoint, (void**)&g_origGetPlayersNearPoint);
#endif

	// func that reads neteventmgr by player idx, crashes page heap
#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("80 7A ? FF 48 8B EA 48 8B F1 0F", -0x13), UnkEventMgr, (void**)&g_origUnkEventMgr);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("4C 69 F0 ? ? ? ? 45 39 BC 3E ? ? ? ? 0F", -0x38), UnkEventMgr, (void**)&g_origUnkEventMgr);
#endif

	// return to disable breaking hooks
	//return;

	{
#ifdef GTA_FIVE
		auto location = hook::get_pattern("44 89 BE B4 00 00 00 FF 90", 7);
#elif IS_RDR3
		auto location = hook::get_pattern("45 8D 41 03 FF 90 ? ? ? ? 84 C0 0F", 4);
#endif
		hook::nop(location, 6);
		hook::call(location, m158Stub);
	}

	{
#ifdef GTA_FIVE
		auto location = hook::get_pattern("48 8B CF FF 90 70 01 00 00 84 C0 74 12 48 8B CF", 3);
		hook::nop(location, 6);

		// m170 in 1604 now
		hook::call(location, m168Stub);
#endif
	}

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("33 DB 48 8B F9 48 39 99 ? ? 00 00 74 ? 48 81 C1 E0", -10), AllocateNetPlayer, (void**)&g_origAllocateNetPlayer);

	MH_CreateHook(hook::get_pattern("8A 41 49 3C FF 74 17 3C 20 73 13 0F B6 C8"), netObject__GetPlayerOwner, (void**)&g_origGetOwnerNetPlayer);
	MH_CreateHook(hook::get_pattern("8A 41 4A 3C FF 74 17 3C 20 73 13 0F B6 C8"), netObject__GetPendingPlayerOwner, (void**)&g_origGetPendingPlayerOwner);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("48 39 99 ? ? ? ? 74 ? 48 81 C1 ? ? ? ? 48 8B 19 48 85", -15), AllocateNetPlayer, (void**)&g_origAllocateNetPlayer);

	MH_CreateHook(hook::get_pattern("80 79 45 20 72 ? 33 C0 C3"), netObject__GetPlayerOwner, (void**)&g_origGetOwnerNetPlayer);
	MH_CreateHook(hook::get_pattern("0F B6 C8 48 8B 05 ? ? ? ? 48 8B 84 C8", -11), netObject__GetPendingPlayerOwner, (void**)&g_origGetPendingPlayerOwner);
#endif

	// function is only 4 bytes, can't be hooked like this
	//MH_CreateHook(hook::get_call(hook::get_pattern("FF 50 68 49 8B CE E8 ? ? ? ? 48 8B 05", 6)), netObject__GetPlayerOwnerId, (void**)&g_origGetOwnerPlayerId);

	// NETWORK_GET_PLAYER_INDEX_FROM_PED
	{
		auto location = hook::get_pattern("48 85 C9 74 ? E8 ? ? ? ? 0F B6 C0 EB", 5);
		hook::set_call(&g_origGetOwnerPlayerId, location);
		hook::call(location, netObject__GetPlayerOwnerId);
	}

#ifdef GTA_FIVE
	hook::jump(hook::get_pattern("C6 41 4A FF C3", 0), netObject__ClearPendingPlayerIndex);
#elif IS_RDR3
	hook::jump(hook::get_pattern("C6 41 46 FF C3", 0), netObject__ClearPendingPlayerIndex);
#endif

	// replace joining local net player to bubble
	{
#ifdef GTA_FIVE
		auto location = hook::get_pattern("48 8B D0 E8 ? ? ? ? E8 ? ? ? ? 83 BB ? ? ? ? 04", 3);
#elif IS_RDR3
		auto location = hook::get_pattern("48 85 C9 74 ? 4C 8D 44 24 40 40 88 7C", 18);
#endif

		hook::set_call(&g_origJoinBubble, location);
		hook::call(location, JoinPhysicalPlayerOnHost);
	}

	{
#ifdef GTA_FIVE
		if (!xbr::IsGameBuildOrGreater<2060>())
		{
			auto match = hook::pattern("80 F9 20 73 13 48 8B").count(2);
			MH_CreateHook(match.get(0).get<void>(0), GetPlayerByIndex, (void**)&g_origGetPlayerByIndex);
			MH_CreateHook(match.get(1).get<void>(0), GetPlayerByIndex, nullptr);
		}
		else
		{
			auto match = hook::pattern("80 F9 20 73 13 48 8B").count(1);
			MH_CreateHook(match.get(0).get<void>(0), GetPlayerByIndex, (void**)&g_origGetPlayerByIndex);

			// #2 is arxan in 1868
			MH_CreateHook(hook::get_call(hook::get_pattern("40 0F 92 C7 40 84 FF 0F 85 ? ? ? ? 40 8A CE E8", 16)), GetPlayerByIndex, nullptr);
		}
#elif IS_RDR3
		auto match = hook::pattern("80 F9 20 73 13 48 8B").count(2);
		MH_CreateHook(match.get(0).get<void>(0), GetPlayerByIndex, (void**)&g_origGetPlayerByIndex);
		MH_CreateHook(match.get(1).get<void>(0), GetPlayerByIndex, nullptr);
#endif
	}

	MH_CreateHook(hook::get_pattern("48 83 EC 28 33 C0 38 05 ? ? ? ? 74 0A"), GetPlayerByIndexNet, (void**)&g_origGetPlayerByIndexNet);

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("75 07 85 C9 0F 94 C3 EB", -0x19), IsNetworkPlayerActive, (void**)&g_origIsNetworkPlayerActive);
	MH_CreateHook(hook::get_pattern("75 07 85 C9 0F 94 C0 EB", -0x13), IsNetworkPlayerConnected, (void**)&g_origIsNetworkPlayerConnected); // connected
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("75 0A 85 C9 0F 94 C3 E9", -0x22), IsNetworkPlayerActive, (void**)&g_origIsNetworkPlayerActive);
	MH_CreateHook(hook::get_pattern("80 3D ? ? ? ? 00 75 ? 85 C9 0F 94 C0 EB", -0x9), IsNetworkPlayerConnected, (void**)&g_origIsNetworkPlayerConnected);
#endif

	MH_CreateHook(hook::get_pattern("84 C0 74 0B 8A 9F ? ? 00 00", -0x14), netPlayer_IsActiveStub, (void**)&g_origNetPlayer_IsActive);

	{
#ifdef GTA_FIVE
		auto location = hook::get_pattern<char>("44 0F 28 CF F3 41 0F 59 C0 F3 44 0F 59 CF F3 44 0F 58 C8 E8", 19);
		MH_CreateHook(hook::get_call(location + 0), netInterface_GetNumRemotePhysicalPlayers, (void**)&g_origGetNetworkPlayerListCount);
		MH_CreateHook(hook::get_call(location + 8), netInterface_GetRemotePhysicalPlayers, (void**)&g_origGetNetworkPlayerList);
#elif IS_RDR3
		auto location = hook::get_pattern<char>("48 8B 07 48 8B CF FF 90 ? ? ? ? 44 8B F8 E8");
		MH_CreateHook(hook::get_call(location + 15), netInterface_GetNumRemotePhysicalPlayers, (void**)&g_origGetNetworkPlayerListCount);
		MH_CreateHook(hook::get_call(location + 22), netInterface_GetRemotePhysicalPlayers, (void**)&g_origGetNetworkPlayerList);
#endif
	}

	{
#ifdef GTA_FIVE
		auto location = hook::get_pattern<char>("48 8B F0 85 DB 74 56 8B", -0x34);
		MH_CreateHook(hook::get_call(location + 0x28), netInterface_GetNumPhysicalPlayers, (void**)&g_origGetNetworkPlayerListCount2);
		MH_CreateHook(hook::get_call(location + 0x2F), netInterface_GetAllPhysicalPlayers, (void**)&g_origGetNetworkPlayerList2);
#elif IS_RDR3
		auto location = hook::get_pattern<char>("8A 43 30 3C 04 75 ? E8");
		MH_CreateHook(hook::get_call(location + 0x7), netInterface_GetNumPhysicalPlayers, (void**)&g_origGetNetworkPlayerListCount2);
		MH_CreateHook(hook::get_call(location + 0xF), netInterface_GetAllPhysicalPlayers, (void**)&g_origGetNetworkPlayerList2);
#endif
	}

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("48 85 DB 74 20 48 8B 03 48 8B CB FF 50 30 48 8B", -0x34), (xbr::IsGameBuildOrGreater<2060>()) ? GetPlayerFromGamerId<2060> : GetPlayerFromGamerId<1604>, (void**)&g_origGetPlayerFromGamerId);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("48 85 DB 74 20 48 8B 03 48 8B CB FF 50 ? 48 8B", -0x27), GetPlayerFromGamerId, (void**)&g_origGetPlayerFromGamerId);
#endif

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("4C 8B F9 74 7D", -0x2B), netObjectMgr__CountObjects, (void**)&g_origCountObjects);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("F6 86 D8 08 00 00 01 74 ? 44 8A 05", -0x3B), netObjectMgr__CountObjects, (void**)&g_origCountObjects);
#endif

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("0F 29 70 C8 4D 8B E1 4D 8B E8", -0x1C), networkBandwidthMgr_CalculatePlayerUpdateLevelsStub, (void**)&g_origNetworkBandwidthMgr_CalculatePlayerUpdateLevels);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("0F 29 78 B8 4D 8B E0 48 8B F2 48 8B F9 33 DB E8", -0x23), networkBandwidthMgr_CalculatePlayerUpdateLevelsStub, (void**)&g_origNetworkBandwidthMgr_CalculatePlayerUpdateLevels);
#endif

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("78 18 4C 8B 05", -10), GetScenarioTaskScenario, (void**)&g_origGetScenarioTaskScenario);

	// #TODO1S: fix player/ped groups so we don't need this workaround anymore
	MH_CreateHook(hook::get_call(hook::get_pattern("48 83 C1 10 48 C1 E0 06 48 03 C8 E8", -21)), GetNetObjPlayerGroup, (void**)&g_origGetNetObjPlayerGroup);
#endif

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("45 8D 65 20 C6 81 ? ? 00 00 01 48 8D 59 08", -0x2F), ObjectManager_End, (void**)&g_origObjectManager_End);
	MH_CreateHook(hook::get_call(hook::get_call(hook::get_pattern<char>("48 8D 05 ? ? ? ? 48 8B D9 48 89 01 E8 ? ? ? ? 84 C0 74 08 48 8B CB E8", -0x19) + 0x32)), PlayerManager_End, (void**)&g_origPlayerManager_End);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("C6 81 ? ? ? ? 01 48 8B CD E8 ? ? ? ? 45 8D 67", -0x36), ObjectManager_End, (void**)&g_origObjectManager_End);
	MH_CreateHook(hook::get_call(hook::get_call(hook::get_pattern<char>("48 8D 05 ? ? ? ? 48 8B F9 48 89 01 E8 ? ? ? ? 84 C0 74 08 48 8B ? E8", -0xF) + 0x28)), PlayerManager_End, (void**)&g_origPlayerManager_End);
#endif

	// disable voice chat bandwidth estimation for 1s (it will overwrite some memory)
#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("40 8A 72 ? 40 80 FE FF 0F 84", -0x46), VoiceChatMgr_EstimateBandwidth, (void**)&g_origVoiceChatMgr_EstimateBandwidth);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("0F B6 72 ? F6 84 B3 ? ? ? ? 01 75", -0x57), VoiceChatMgr_EstimateBandwidth, (void**)&g_origVoiceChatMgr_EstimateBandwidth);
#endif

#ifdef GTA_FIVE
	// crash logging for invalid mount indices
	MH_CreateHook(hook::get_pattern("48 8B FA 48 8D 91 44 01 00 00 48 8B F1", -0x16), CPedGameStateDataNode__access, (void**)&g_origCPedGameStateDataNode__access);

	// getnetplayerped 32 cap
	hook::nop(hook::get_pattern("83 F9 1F 77 26 E8", 3), 2);
#endif

	// always allow to migrate, even if not cloned on bit test
#ifdef GTA_FIVE
	hook::put<uint8_t>(hook::get_pattern("75 29 48 8B 02 48 8B CA FF 50 30"), 0xEB);
#elif IS_RDR3
	hook::put<uint8_t>(hook::get_pattern("75 29 48 8B 06 48 8B CE FF 50 60"), 0xEB);
#endif

	// delete objects from clonemgr before deleting them
	static struct : public jitasm::Frontend
	{
		virtual void InternalMain() override
		{
#ifdef GTA_FIVE
			mov(rcx, rbx);
			mov(rax, (uintptr_t)&DeletionMethod);
			jmp(rax);
#elif IS_RDR3
			mov(rcx, rsi);
			mov(rax, (uintptr_t)&DeletionMethod);
			jmp(rax);
#endif
		}

		static void DeletionMethod(rage::netObject* object)
		{
			if (icgi->OneSyncEnabled)
			{
				TheClones->OnObjectDeletion(object);
			}

			delete object;
		}
	} delStub;

#ifdef GTA_FIVE
	hook::call(hook::get_pattern("48 C1 EA 04 E8 ? ? ? ? 48 8B 03", 17), delStub.GetCode());
#elif IS_RDR3
	hook::call(hook::get_pattern("48 8B 06 BA 01 00 00 00 48 8B CE FF 10 48 8B 5C 24 50", 8), delStub.GetCode());
#endif

	// clobber nodes for all players, not just when connected to netplayermgr
	// not working, maybe?
#ifdef GTA_FIVE
	hook::nop(hook::get_pattern("0F A3 C7 73 18 44", 3), 2);
#elif IS_RDR3
	hook::nop(hook::get_pattern("8B 44 84 58 0F A3 D0 73", 7), 2);
#endif

	// always write up-to-date data to nodes, not the cached data from syncdata
	{
#ifdef GTA_FIVE
		auto location = hook::get_pattern("FF 90 D0 00 00 00 84 C0 0F 84 80 00 00 00", 0);
#elif IS_RDR3
		auto location = hook::get_pattern("49 8B D5 48 8B CF FF 90 ? ? ? ? 84 ? 74", 6);
#endif

		hook::nop(location, 6);
		hook::call(location, mD0Stub); // (RDR3) in 1207 it was 0xB0, in 1311 it's 0xC0
	}

#ifdef GTA_FIVE
	// hardcoded 32/128 array sizes in CNetObjEntity__TestProximityMigration
	{
		auto location = hook::get_pattern<char>("48 81 EC A0 01 00 00 33 DB 48 8B F9 38 1D", -0x15);

		// 0x20: scratch space
		// 256 * 8: 256 players, ptr size
		// 256 * 4: 256 players, int size
		auto stackSize = (0x20 + (256 * 8) + (256 * 4));
		auto ptrsBase = 0x20;
		auto intsBase = ptrsBase + (256 * 8);

		hook::put<uint32_t>(location + 0x18, stackSize);
		hook::put<uint32_t>(location + 0xF8, stackSize);

		hook::put<uint32_t>(location + 0xC9, intsBase);
	}

	// same for CNetObjPed
	{
		auto location = hook::get_pattern<char>("48 81 EC A0 01 00 00 33 FF 48 8B D9", -0x15);

		auto stackSize = (0x20 + (256 * 8) + (256 * 4));
		auto ptrsBase = 0x20;
		auto intsBase = ptrsBase + (256 * 8);

		hook::put<uint32_t>(location + 0x18, stackSize);
		hook::put<uint32_t>(location + 0x13C, stackSize);

		hook::put<uint32_t>(location + 0xD5, intsBase);
	}

	// 32 array size somewhere called by CTaskNMShot
	{
		auto location = hook::get_pattern<char>("48 8D A8 38 FF FF FF 48 81 ? ? ? ? 00 80 3D", -0x1C);

		hook::put<uint32_t>(location + 0x26, 0xA0 + (256 * 8));
		hook::put<uint32_t>(location + 0x269, 0xA0 + (256 * 8));
	}

	// 32 array size for network object limiting
	// #TODO: unwind info for these??
	if (!Is372() && !xbr::IsGameBuildOrGreater<2060>()) // only validated for 1604 so far
	{
		auto location = hook::get_pattern<char>("48 85 C0 0F 84 C3 06 00 00 E8", -0x4A);

		// stack frame ENTER
		hook::put<uint32_t>(location + 0x19, 0xE58);

		// stack frame LEAVE
		hook::put<uint32_t>(location + 0x71A, 0xE58);

		// var: rsp+1A0
		hook::put<uint32_t>(location + 0x3A8, 0xDA0);
		hook::put<uint32_t>(location + 0x3C4, 0xDA0);
		hook::put<uint32_t>(location + 0x40E, 0xDA0);
		hook::put<uint32_t>(location + 0x41D, 0xDA0);

		// var: rsp+1A8
		hook::put<uint32_t>(location + 0x3B6, 0xDA8);
		hook::put<uint32_t>(location + 0x3CB, 0xDA8);
		hook::put<uint32_t>(location + 0x427, 0xDA8);
		hook::put<uint32_t>(location + 0x431, 0xDA8);

		// var: rsp+1B0
		hook::put<uint32_t>(location + 0x3AF, 0xDB0);
		hook::put<uint32_t>(location + 0x3D2, 0xDB0);
		hook::put<uint32_t>(location + 0x440, 0xDB0);
		hook::put<uint32_t>(location + 0x453, 0xDB0);

		// var: rsp+1B8
		hook::put<uint32_t>(location + 0xA9, 0xDB8);
		hook::put<uint32_t>(location + 0x366, 0xDB8);
		hook::put<uint32_t>(location + 0x555, 0xDB8);

		// player indices
		hook::put<uint8_t>(location + 0x467, 0x7F); // 127.
		hook::put<uint8_t>(location + 0x39E, 0x7F);
	}

	// CNetworkDamageTracker float[32] array
	// (would overflow into pool data and be.. quite bad)
	{
		// pool constructor call
		hook::put<uint32_t>(hook::get_pattern("C7 44 24 20 88 00 00 00 E8", 4), sizeof(void*) + (sizeof(float) * 256));

		// memset in CNetworkDamageTracker::ctor
		hook::put<uint32_t>(hook::get_pattern("48 89 11 48 8B D9 41 B8 80 00 00 00", 8), sizeof(float) * 256);
	}
#endif

	MH_EnableHook(MH_ALL_HOOKS);
});

// event stuff
static void* g_netEventMgr;

#ifdef IS_RDR3
static std::map<uint16_t, const char*> g_eventNames;
#endif

namespace rage
{
	class netGameEvent
	{
	public:
		virtual ~netGameEvent() = 0;

#ifdef GTA_FIVE
		virtual const char* GetName() = 0;
#endif

		virtual bool IsInScope(CNetGamePlayer* player) = 0;

		virtual bool TimeToResend(uint32_t) = 0;

		virtual bool CanChangeScope() = 0;

		virtual void Prepare(rage::datBitBuffer* buffer, CNetGamePlayer* player, CNetGamePlayer* unkPlayer) = 0;

		virtual void Handle(rage::datBitBuffer* buffer, CNetGamePlayer* player, CNetGamePlayer* unkPlayer) = 0;

		virtual bool Decide(CNetGamePlayer* sourcePlayer, void* connUnk) = 0;

		virtual void PrepareReply(rage::datBitBuffer* buffer, CNetGamePlayer* replyPlayer) = 0;

		virtual void HandleReply(rage::datBitBuffer* buffer, CNetGamePlayer* sourcePlayer) = 0;

#ifdef GTA_FIVE
		virtual void PrepareExtraData(rage::datBitBuffer* buffer, bool isReply, CNetGamePlayer* player, CNetGamePlayer* unkPlayer) = 0;

		virtual void HandleExtraData(rage::datBitBuffer* buffer, bool isReply, CNetGamePlayer* player, CNetGamePlayer* unkPlayer) = 0;
#elif IS_RDR3
		virtual void PrepareExtraData(rage::datBitBuffer* buffer, CNetGamePlayer* player, CNetGamePlayer* unkPlayer) = 0;

		virtual void HandleExtraData(rage::datBitBuffer* buffer, CNetGamePlayer* player, CNetGamePlayer* unkPlayer) = 0;
#endif

#ifdef GTA_FIVE
		virtual void m_60() = 0;

		virtual void m_68() = 0;

		virtual void m_70() = 0;

		virtual void m_78() = 0;
#endif

		virtual bool Equals(const netGameEvent* event) = 0;

		virtual bool NotEquals(const netGameEvent* event) = 0;

		virtual bool MustPersist() = 0;

		virtual bool MustPersistWhenOutOfScope() = 0;

		virtual bool HasTimedOut() = 0;

	public:
		uint16_t eventType; // +0x8

		uint8_t requiresReply : 1; // +0xA

		uint8_t pad_0Bh; // +0xB

#ifdef GTA_FIVE
		char pad_0Ch[24]; // +0xC
#elif IS_RDR3
		char pad_0Ch[36]; // +0xC
#endif

		uint16_t eventId; // +0x24

		uint8_t hasEventId : 1; // +0x26

#ifdef IS_RDR3
	public:
		const char* GetName()
		{
			auto findEvent = g_eventNames.find(eventType);

			if (findEvent == g_eventNames.end())
			{
				assert("Unknown event name");
				return "UNKNOWN_EVENT";
			}

			return findEvent->second;
		}
#endif
	};
}

static CNetGamePlayer* g_player31;

bool EnsurePlayer31()
{
	if (!g_player31)
	{
		g_player31 = AllocateNetPlayer(nullptr);
		g_player31->physicalPlayerIndex() = 31;
	}

	return (g_player31 != nullptr);
}

#include <chrono>

using namespace std::chrono_literals;

inline std::chrono::milliseconds msec()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
}

// TODO: event expiration?
struct netGameEventState
{
	rage::netGameEvent* ev;
	std::chrono::milliseconds time;
	bool sent;

	netGameEventState()
		: ev(nullptr), time(0), sent(false)
	{

	}

	netGameEventState(rage::netGameEvent* ev, std::chrono::milliseconds time)
		: ev(ev), time(time), sent(false)
	{

	}
};

static std::map<std::tuple<uint16_t, uint16_t>, netGameEventState> g_events;

static void(*g_origAddEvent)(void*, rage::netGameEvent*);
static uint16_t g_eventHeader;

static void EventMgr_AddEvent(void* eventMgr, rage::netGameEvent* ev)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origAddEvent(eventMgr, ev);
	}

	// don't give control using events!
	if (strcmp(ev->GetName(), "GIVE_CONTROL_EVENT") == 0)
	{
		delete ev;
		return;
	}

	if (strcmp(ev->GetName(), "ALTER_WANTED_LEVEL_EVENT") == 0)
	{
		// do we already have 5 ALTER_WANTED_LEVEL_EVENT instances?
		int count = 0;

		for (auto& eventPair : g_events)
		{
			auto [key, tup] = eventPair;

			if (tup.ev && strcmp(tup.ev->GetName(), "ALTER_WANTED_LEVEL_EVENT") == 0)
			{
				count++;
			}
		}

		if (count >= 5)
		{
			delete ev;
			return;
		}
	}

	// is this a duplicate event?
	for (auto& eventPair : g_events)
	{
		auto [key, tup] = eventPair;

		if (tup.ev && tup.ev->Equals(ev))
		{
			delete ev;
			return;
		}
	}

	auto eventId = (ev->hasEventId) ? ev->eventId : g_eventHeader++;

	auto [ it, inserted ] = g_events.insert({ { ev->eventType, eventId }, { ev, msec() } });

	if (!inserted)
	{
		delete ev;
	}
}

static bool EventNeedsOriginalPlayer(rage::netGameEvent* ev)
{
	auto nameHash = HashString(ev->GetName());

	// synced scenes depend on this to target the correct remote player
	if (nameHash == HashString("REQUEST_NETWORK_SYNCED_SCENE_EVENT") ||
		nameHash == HashString("START_NETWORK_SYNCED_SCENE_EVENT") ||
		nameHash == HashString("STOP_NETWORK_SYNCED_SCENE_EVENT") ||
		nameHash == HashString("UPDATE_NETWORK_SYNCED_SCENE_EVENT"))
	{
		return true;
	}

	return false;
}

static void SendGameEventRaw(uint16_t eventId, rage::netGameEvent* ev)
{
	// TODO: use a real player for some things
	EnsurePlayer31();

	// allocate a RAGE buffer
	uint8_t packetStub[1024];
	rage::datBitBuffer rlBuffer(packetStub, sizeof(packetStub));

	ev->Prepare(&rlBuffer, g_player31, nullptr);

#ifdef GTA_FIVE
	ev->PrepareExtraData(&rlBuffer, false, g_player31, nullptr);
#elif IS_RDR3
	ev->PrepareExtraData(&rlBuffer, g_player31, nullptr);
#endif

	net::Buffer outBuffer;

	// TODO: replace with bit array?
	std::set<int> targetPlayers;

	for (auto& player : g_players)
	{
		if (
			player
#ifdef GTA_FIVE
			&& player->nonPhysicalPlayerData()
#endif
		)
		{
			// temporary pointer check
#ifdef GTA_FIVE
			if ((uintptr_t)player->nonPhysicalPlayerData() > 256)
#endif
			{
				// make it 31 for a while (objectmgr dependencies mandate this)
				auto originalIndex = player->physicalPlayerIndex();

				if (!EventNeedsOriginalPlayer(ev))
				{
					player->physicalPlayerIndex() = (player != g_playerMgr->localPlayer) ? 31 : 0;
				}

				if (ev->IsInScope(player))
				{
					targetPlayers.insert(g_netIdsByPlayer[player]);
				}

				player->physicalPlayerIndex() = originalIndex;
			}
#ifdef GTA_FIVE
			else
			{
				AddCrashometry("player_corruption", "true");
			}
#endif
		}
	}

	outBuffer.Write<uint8_t>(targetPlayers.size());

	for (int playerId : targetPlayers)
	{
		outBuffer.Write<uint16_t>(playerId);
	}

	outBuffer.Write<uint16_t>(eventId);
	outBuffer.Write<uint8_t>(0);
	outBuffer.Write<uint16_t>(ev->eventType);

	uint32_t len = rlBuffer.GetDataLength();
	outBuffer.Write<uint16_t>(len); // length (short)
	outBuffer.Write(rlBuffer.m_data, len); // data

	g_netLibrary->SendReliableCommand("msgNetGameEvent", (const char*)outBuffer.GetData().data(), outBuffer.GetCurOffset());
}

static atPoolBase** g_netGameEventPool;

static std::deque<net::Buffer> g_reEventQueue;
static void HandleNetGameEvent(const char* idata, size_t len);

static void EventManager_Update()
{
#ifdef IS_RDR3
	if (!g_netGameEventPool)
	{
		auto pool = rage::GetPoolBase(0xF4794A41);

		if (pool)
		{
			g_netGameEventPool = &pool;
		}
	}
#endif

	if (!g_netGameEventPool)
	{
		return;
	}

	std::set<decltype(g_events)::key_type> toRemove;

	for (auto& eventPair : g_events)
	{
		auto& evSet = eventPair.second;
		auto ev = evSet.ev;

		if (ev)
		{
			if (!evSet.sent)
			{
				SendGameEventRaw(std::get<1>(eventPair.first), ev);

				evSet.sent = true;
			}

			auto expiryDuration = 5s;

			if (ev->HasTimedOut() || (msec() - evSet.time) > expiryDuration)
			{
				delete ev;

				toRemove.insert(eventPair.first);
			}
		}
	}

	for (auto var : toRemove)
	{
		g_events.erase(var);
	}

	// re-events
	std::vector<net::Buffer> reEvents;

	while (!g_reEventQueue.empty())
	{
		reEvents.push_back(std::move(g_reEventQueue.front()));
		g_reEventQueue.pop_front();
	}

	for (auto& evBuf : reEvents)
	{
		evBuf.Seek(0);
		HandleNetGameEvent(reinterpret_cast<const char*>(evBuf.GetBuffer()), evBuf.GetLength());
	}
}

static bool g_lastEventGotRejected;

static void HandleNetGameEvent(const char* idata, size_t len)
{
	if (!icgi->HasVariable("networkInited"))
	{
		return;
	}

	// TODO: use a real player for some things that _are_ 32-safe
	EnsurePlayer31();

	net::Buffer buf(reinterpret_cast<const uint8_t*>(idata), len);
	auto sourcePlayerId = buf.Read<uint16_t>();
	auto eventHeader = buf.Read<uint16_t>();
	auto isReply = buf.Read<uint8_t>();
	auto eventType = buf.Read<uint16_t>();
	auto length = buf.Read<uint16_t>();

	auto player = g_playersByNetId[sourcePlayerId];

	if (!player)
	{
		player = g_player31;
	}

	std::vector<uint8_t> data(length);
	buf.Read(data.data(), data.size());

	rage::datBitBuffer rlBuffer(const_cast<uint8_t*>(data.data()), data.size());
	rlBuffer.m_f1C = 1;

#ifdef GTA_FIVE
	static auto maxEvent = (xbr::IsGameBuildOrGreater<2060>() ? 0x5B : 0x5A);
#elif IS_RDR3
	static auto maxEvent = 0xA5;
#endif

	if (eventType > maxEvent)
	{
		return;
	}

	if (isReply)
	{
		auto evSetIt = g_events.find({ eventType, eventHeader });

		if (evSetIt != g_events.end())
		{
			auto ev = evSetIt->second.ev;

			if (ev)
			{
				ev->HandleReply(&rlBuffer, player);

#ifdef GTA_FIVE
				ev->HandleExtraData(&rlBuffer, true, player, g_playerMgr->localPlayer);
#elif IS_RDR3
				ev->HandleExtraData(&rlBuffer, player, g_playerMgr->localPlayer);
#endif

				delete ev;
				g_events.erase({ eventType, eventHeader });
			}
		}
	}
	else
	{
		using TEventHandlerFn = void(*)(rage::datBitBuffer* buffer, CNetGamePlayer* player, CNetGamePlayer* unkConn, uint16_t, uint32_t, uint32_t);

		bool rejected = false;

		// for all intents and purposes, the player will be 31
		auto lastIndex = player->physicalPlayerIndex();
		player->physicalPlayerIndex() = 31;

		auto eventMgr = *(char**)g_netEventMgr;

		if (eventMgr)
		{
#ifdef GTA_FIVE
			auto eventHandlerList = (TEventHandlerFn*)(eventMgr + (xbr::IsGameBuildOrGreater<2060>() ? 0x3ABD0 : 0x3AB80));
#elif IS_RDR3
			auto eventHandlerList = (TEventHandlerFn*)(eventMgr + 0x3BF10);
#endif

			auto eh = eventHandlerList[eventType];

			if (eh && (uintptr_t)eh >= hook::get_adjusted(0x140000000) && (uintptr_t)eh < hook::get_adjusted(0x146000000))
			{
				eh(&rlBuffer, player, g_playerMgr->localPlayer, eventHeader, 0, 0);
				rejected = g_lastEventGotRejected;
			}
		}

		player->physicalPlayerIndex() = lastIndex;

		if (rejected)
		{
			g_reEventQueue.push_back(buf);
		}
	}
}

static void DecideNetGameEvent(rage::netGameEvent* ev, CNetGamePlayer* player, CNetGamePlayer* unkConn, rage::datBitBuffer* buffer, uint16_t evH)
{
	g_lastEventGotRejected = false;

	if (ev->Decide(player, unkConn))
	{
#ifdef GTA_FIVE
		ev->HandleExtraData(buffer, false, player, unkConn);
#elif IS_RDR3
		ev->HandleExtraData(buffer, player, unkConn);
#endif

		if (ev->requiresReply)
		{
			uint8_t packetStub[1024];
			rage::datBitBuffer rlBuffer(packetStub, sizeof(packetStub));

			ev->PrepareReply(&rlBuffer, player);

#ifdef GTA_FIVE
			ev->PrepareExtraData(&rlBuffer, true, player, nullptr);
#elif IS_RDR3
			ev->PrepareExtraData(&rlBuffer, player, nullptr);
#endif

			net::Buffer outBuffer;
			outBuffer.Write<uint8_t>(1);
			outBuffer.Write<uint16_t>(g_netIdsByPlayer[player]);

			outBuffer.Write<uint16_t>(evH);
			outBuffer.Write<uint8_t>(1);
			outBuffer.Write<uint16_t>(ev->eventType);

			uint32_t len = rlBuffer.GetDataLength();
			outBuffer.Write<uint16_t>(len); // length (short)
			outBuffer.Write(rlBuffer.m_data, len); // data

			g_netLibrary->SendReliableCommand("msgNetGameEvent", (const char*)outBuffer.GetData().data(), outBuffer.GetCurOffset());
		}
	}
	else
	{
		g_lastEventGotRejected = !ev->HasTimedOut() && ev->MustPersist();
	}
}

static void(*g_origExecuteNetGameEvent)(void* eventMgr, rage::netGameEvent* ev, rage::datBitBuffer* buffer, CNetGamePlayer* player, CNetGamePlayer* unkConn, uint16_t evH, uint32_t, uint32_t);

static void ExecuteNetGameEvent(void* eventMgr, rage::netGameEvent* ev, rage::datBitBuffer* buffer, CNetGamePlayer* player, CNetGamePlayer* unkConn, uint16_t evH, uint32_t a, uint32_t b)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origExecuteNetGameEvent(eventMgr, ev, buffer, player, unkConn, evH, a, b);
	}

	ev->Handle(buffer, player, unkConn);

	// missing: some checks
	DecideNetGameEvent(ev, player, unkConn, buffer, evH);
}

static InitFunction initFunctionEv([]()
{
	NetLibrary::OnNetLibraryCreate.Connect([](NetLibrary* netLibrary)
	{
		icgi = Instance<ICoreGameInit>::Get();

		netLibrary->OnClientInfoReceived.Connect([](const NetLibraryClientInfo& info)
		{
			if (!icgi->OneSyncEnabled)
			{
				return;
			}

			if (g_players[info.slotId])
			{
				console::DPrintf("onesync", "Dropping duplicate player for slotID %d.\n", info.slotId);
				HandleClientDrop(info);
			}

			if (g_playersByNetId[info.netId])
			{
				auto tempInfo = info;
				tempInfo.slotId = g_playersByNetId[info.netId]->physicalPlayerIndex();

				console::Printf("onesync", "Dropping duplicate player for netID %d (slotID %d).\n", info.netId, tempInfo.slotId);
				HandleClientDrop(tempInfo);
			}

			HandleClientInfo(info);
		});

		netLibrary->OnClientInfoDropped.Connect([](const NetLibraryClientInfo& info)
		{
			if (!icgi->OneSyncEnabled)
			{
				return;
			}

			HandleClientDrop(info);
		});

		netLibrary->AddReliableHandler("msgNetGameEvent", [](const char* data, size_t len)
		{
			if (!icgi->OneSyncEnabled)
			{
				return;
			}

			HandleNetGameEvent(data, len);
		}, true);
	});
});

static bool(*g_origSendGameEvent)(void*, void*);

static bool SendGameEvent(void* eventMgr, void* ev)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origSendGameEvent(eventMgr, ev);
	}

	return 1;
}

#if GTA_FIVE
static uint32_t(*g_origGetFireApplicability)(void* event, void* pos);

static uint32_t GetFireApplicability(void* event, void* pos)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetFireApplicability(event, pos);
	}

	// send all fires to all remote players
	return (1 << 31);
}
#endif

static hook::thiscall_stub<bool(void* eventMgr, bool fatal)> rage__netEventMgr__CheckForSpaceInPool([]()
{
#ifdef GTA_FIVE
	return hook::get_pattern("41 C1 E0 02 41 C1 F8 02 41 2B C0 0F 85", -0x2A);
#elif IS_RDR3
	return hook::get_pattern("33 DB 85 C0 0F 85 ? ? ? ? 40 84 FF 0F 84", -0x30);
#endif
});

#ifdef GTA_FIVE
static void (*g_origSendAlterWantedLevelEvent1)(void*, void*, void*, void*);

static void SendAlterWantedLevelEvent1Hook(void* a1, void* a2, void* a3, void* a4)
{
	if (!rage__netEventMgr__CheckForSpaceInPool(g_netEventMgr, false))
	{
		return;
	}

	g_origSendAlterWantedLevelEvent1(a1, a2, a3, a4);
}

static void (*g_origSendAlterWantedLevelEvent2)(void*, void*, void*, void*);

static void SendAlterWantedLevelEvent2Hook(void* a1, void* a2, void* a3, void* a4)
{
	if (!rage__netEventMgr__CheckForSpaceInPool(g_netEventMgr, false))
	{
		return;
	}

	g_origSendAlterWantedLevelEvent2(a1, a2, a3, a4);
}
#endif

#ifdef GTA_FIVE
std::string GetType(void* d);

static void NetEventError()
{
	auto pool = rage::GetPoolBase("netGameEvent");

	std::map<std::string, int> poolCount;

	for (int i = 0; i < pool->GetSize(); i++)
	{
		auto e = pool->GetAt<void>(i);

		if (e)
		{
			poolCount[GetType(e)]++;
		}
	}

	std::vector<std::pair<int, std::string>> entries;

	for (const auto& [ type, count ] : poolCount)
	{
		entries.push_back({ count, type });
	}

	std::sort(entries.begin(), entries.end(), [](const auto& l, const auto& r)
	{
		return r.first < l.first;
	});

	std::string poolSummary;

	for (const auto& [count, type] : entries)
	{
		poolSummary += fmt::sprintf("  %s: %d entries\n", type, count);
	}

	FatalError("Ran out of rage::netGameEvent pool space.\n\nPool usage:\n%s", poolSummary);
}
#endif

#ifdef IS_RDR3
static void* (*g_origRegisterGameEvent)(void*, uint16_t, void*, const char*);

static void* RegisterGameEvent(void* eventMgr, uint16_t eventId, void* func, const char* name)
{
	g_eventNames.insert({ eventId, name });
	return g_origRegisterGameEvent(eventMgr, eventId, func, name);
}
#endif

static HookFunction hookFunctionEv([]()
{
	MH_Initialize();

	{
		auto location = hook::get_pattern<char>("E8 ? ? ? ? 48 8B CB E8 ? ? ? ? 84 C0 74 11 48 8B 0D", 0x14);

		g_netEventMgr = hook::get_address<void*>(location);
		MH_CreateHook(hook::get_call(location + 7), EventMgr_AddEvent, (void**)&g_origAddEvent);
	}

	// we hook game event registration for storing event names
#ifdef IS_RDR3
	MH_CreateHook(hook::get_pattern("48 83 C1 08 0F B7 FA E8 ? ? ? ? B8", -0x1A), RegisterGameEvent, (void**)&g_origRegisterGameEvent);
#endif

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("48 8B DA 48 8B F1 41 81 FF 00", -0x2A), ExecuteNetGameEvent, (void**)&g_origExecuteNetGameEvent);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("48 89 6C 24 60 4D 8B F1 49 8B F0 48 8B DA E8", -0x25), ExecuteNetGameEvent, (void**)&g_origExecuteNetGameEvent);
#endif

	// can cause crashes due to high player indices, default event sending
#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("48 83 EC 30 80 7A ? FF 4C 8B D2", -0xC), SendGameEvent, (void**)&g_origSendGameEvent);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("48 83 EC 30 48 8B F9 4C 8B F2 48 83 C1 08", -0xE), SendGameEvent, (void**)&g_origSendGameEvent);
#endif

#ifdef GTA_FIVE
	// fire applicability
	MH_CreateHook(hook::get_pattern("85 DB 74 78 44 8B F3 48", -0x30), GetFireApplicability, (void**)&g_origGetFireApplicability);

	// CAlterWantedLevelEvent pool check
	if (xbr::IsGameBuildOrGreater<2060>())
	{
		MH_CreateHook(hook::get_call(hook::get_pattern("45 8A C4 48 8B C8 41 8B D7", 8)), SendAlterWantedLevelEvent1Hook, (void**)&g_origSendAlterWantedLevelEvent1);
		MH_CreateHook(hook::get_pattern("4C 8B 78 10 48 85 F6", -0x58), SendAlterWantedLevelEvent2Hook, (void**)&g_origSendAlterWantedLevelEvent2);
	}
	else
	{
		MH_CreateHook(hook::get_call(hook::get_pattern("45 8A C6 48 8B C8 8B D5 E8 ? ? ? ? 45 32 E4", 8)), SendAlterWantedLevelEvent1Hook, (void**)&g_origSendAlterWantedLevelEvent1);
		MH_CreateHook(hook::get_pattern("4C 8B 78 10 48 85 ED 74 74 66 39 55", -0x58), SendAlterWantedLevelEvent2Hook, (void**)&g_origSendAlterWantedLevelEvent2);
	}
#endif

	// CheckForSpaceInPool error display
#ifdef GTA_FIVE
	hook::call(hook::get_pattern("33 C9 E8 ? ? ? ? E9 FD FE FF FF", 2), NetEventError);
#endif

	MH_EnableHook(MH_ALL_HOOKS);

#ifdef GTA_FIVE
	{
		auto location = hook::get_pattern("44 8B 40 20 8B 40 10 41 C1 E0 02 41 C1 F8 02 41 2B C0 0F 85", -7);
		g_netGameEventPool = hook::get_address<decltype(g_netGameEventPool)>(location);
	}
#endif
});

#include <nutsnbolts.h>
#include <GameInit.h>

struct VirtualBase
{
	virtual ~VirtualBase() {}
};

struct VirtualDerivative : public VirtualBase
{
	virtual ~VirtualDerivative() override {}
};

std::string GetType(void* d)
{
	VirtualBase* self = (VirtualBase*)d;

#ifdef GTA_FIVE
	std::string typeName = fmt::sprintf("unknown (vtable %p)", *(void**)self);

	try
	{
		typeName = typeid(*self).name();
	}
	catch (std::__non_rtti_object&)
	{

	}
#elif IS_RDR3
	std::string typeName = fmt::sprintf("%p", *(void**)self);
#endif

	return typeName;
}

extern rage::netObject* g_curNetObject;

static char(*g_origReadDataNode)(void* node, uint32_t flags, void* mA0, rage::datBitBuffer* buffer, rage::netObject* object);

std::map<int, std::map<void*, std::tuple<int, uint32_t>>> g_netObjectNodeMapping;

static bool ReadDataNodeStub(void* node, uint32_t flags, void* mA0, rage::datBitBuffer* buffer, rage::netObject* object)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origReadDataNode(node, flags, mA0, buffer, object);
	}

	// enable this for boundary checks
	/*if (flags == 1 || flags == 2)
	{
		uint32_t in = 0;
		buffer->ReadInteger(&in, 8);
		assert(in == 0x5A);
	}*/

	bool didRead = g_origReadDataNode(node, flags, mA0, buffer, object);

	if (didRead && g_curNetObject)
	{
		g_netObjectNodeMapping[g_curNetObject->GetObjectId()][node] = { 0, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp() };
	}

	return didRead;
}

#ifdef GTA_FIVE
static char(*g_origWriteDataNode)(void* node, uint32_t flags, void* mA0, rage::netObject* object, rage::datBitBuffer* buffer, int time, void* playerObj, char playerId, void* unk);

static bool WriteDataNodeStub(void* node, uint32_t flags, void* mA0, rage::netObject* object, rage::datBitBuffer* buffer, int time, void* playerObj, char playerId, void* unk)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origWriteDataNode(node, flags, mA0, object, buffer, time, playerObj, playerId, unk);
	}

	if (playerId != 31 || flags == 4)
	{
		return g_origWriteDataNode(node, flags, mA0, object, buffer, time, playerObj, playerId, unk);
	}
	else
	{
		// save position and write a placeholder length frame
		uint32_t position = buffer->GetPosition();
		buffer->WriteBit(false);
		buffer->WriteUns(0, 11);

		bool rv = g_origWriteDataNode(node, flags, mA0, object, buffer, time, playerObj, playerId, unk);

		// write the actual length on top of the position
		uint32_t endPosition = buffer->GetPosition();
		auto length = endPosition - position - 11 - 1;

		if (length > 1 || flags == 1)
		{
			buffer->Seek(position);

			buffer->WriteBit(true);
			buffer->WriteUns(length, 11);
			buffer->Seek(endPosition);

			if (g_curNetObject)
			{
				g_netObjectNodeMapping[g_curNetObject->GetObjectId()][node] = { 1, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp() };
			}

			//trace("actually wrote %s\n", GetType(node));
		}
		else
		{
			buffer->Seek(position + 1);
		}

		return rv;
	}
}
#elif IS_RDR3
static char(*g_origWriteDataNode)(void* node, uint32_t flags, uint32_t objectFlags, rage::netObject* object, rage::datBitBuffer* buffer, void* playerObj, char playerId, void* a8, void* unk);

static bool WriteDataNodeStub(void* node, uint32_t flags, uint32_t objectFlags, rage::netObject* object, rage::datBitBuffer* buffer, void* playerObj, char playerId, void* a8, void* unk)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origWriteDataNode(node, flags, objectFlags, object, buffer, playerObj, playerId, a8, unk);
	}

	if (playerId != 31 || flags == 4)
	{
		return g_origWriteDataNode(node, flags, objectFlags, object, buffer, playerObj, playerId, a8, unk);
	}
	else
	{
		// save position and write a placeholder length frame
		uint32_t position = buffer->GetPosition();
		buffer->WriteBit(false);
		buffer->WriteUns(0, 11);

		bool rv = g_origWriteDataNode(node, flags, objectFlags, object, buffer, playerObj, playerId, a8, unk);

		// write the actual length on top of the position
		uint32_t endPosition = buffer->GetPosition();
		auto length = endPosition - position - 11 - 1;

		if (length > 1 || flags == 1)
		{
			buffer->Seek(position);

			buffer->WriteBit(true);
			buffer->WriteUns(length, 11);
			buffer->Seek(endPosition);

			if (g_curNetObject)
			{
				g_netObjectNodeMapping[g_curNetObject->GetObjectId()][node] = { 1, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp() };
			}

			//trace("actually wrote %s\n", GetType(node));
		}
		else
		{
			buffer->Seek(position + 1);
		}

		return rv;
	}
}
#endif

static void(*g_origUpdateSyncDataOn108)(void* node, void* object);

static void UpdateSyncDataOn108Stub(void* node, void* object)
{
	//if (!icgi->OneSyncEnabled)
	{
		g_origUpdateSyncDataOn108(node, object);
	}
}

static void(*g_origManuallyDirtyNode)(void* node, void* object);

namespace rage
{
class netSyncDataNodeBase;
}

extern void DirtyNode(rage::netObject* object, rage::netSyncDataNodeBase* node);

static void ManuallyDirtyNodeStub(rage::netSyncDataNodeBase* node, rage::netObject* object)
{
	if (!icgi->OneSyncEnabled)
	{
		g_origManuallyDirtyNode(node, object);
		return;
	}

	DirtyNode(object, node);
}

static void(*g_orig_netSyncDataNode_ForceSend)(void* node, int actFlag1, int actFlag2, rage::netObject* object);

static void netSyncDataNode_ForceSendStub(rage::netSyncDataNodeBase* node, int actFlag1, int actFlag2, rage::netObject* object)
{
	if (!icgi->OneSyncEnabled)
	{
		g_orig_netSyncDataNode_ForceSend(node, actFlag1, actFlag2, object);
		return;
	}

	// maybe needs to read act flags?
	DirtyNode(object, node);
}

static void(*g_orig_netSyncDataNode_ForceSendToPlayer)(void* node, int player, int actFlag1, int actFlag2, rage::netObject* object);

static void netSyncDataNode_ForceSendToPlayerStub(rage::netSyncDataNodeBase* node, int player, int actFlag1, int actFlag2, rage::netObject* object)
{
	if (!icgi->OneSyncEnabled)
	{
		g_orig_netSyncDataNode_ForceSendToPlayer(node, player, actFlag1, actFlag2, object);
		return;
	}

	// maybe needs to read act flags?
	DirtyNode(object, node);
}

static void(*g_origCallSkip)(void* a1, void* a2, void* a3, void* a4, void* a5);

static void SkipCopyIf1s(void* a1, void* a2, void* a3, void* a4, void* a5)
{
	if (!icgi->OneSyncEnabled)
	{
		g_origCallSkip(a1, a2, a3, a4, a5);
	}
}

static HookFunction hookFunction2([]()
{
	// 2 matches, 1st is data, 2nd is parent
	{
#ifdef GTA_FIVE
		auto location = hook::get_pattern<char>("48 89 44 24 20 E8 ? ? ? ? 84 C0 0F 95 C0 48 83 C4 58", -0x3C);
		hook::set_call(&g_origWriteDataNode, location + 0x41);
#elif IS_RDR3
#ifndef ONESYNC_CLONING_NATIVES
		auto location = hook::get_pattern<char>("49 89 43 C8 E8 ? ? ? ? 84 C0 0F 95 C0 48 83 C4 58", -0x3E);
		hook::set_call(&g_origWriteDataNode, location + 0x42);
#endif
#endif

#ifndef ONESYNC_CLONING_NATIVES
		hook::jump(location, WriteDataNodeStub);
#endif
	}

	{
		MH_Initialize();

#ifdef GTA_FIVE
		MH_CreateHook(hook::get_pattern("48 8B 03 48 8B D6 48 8B CB EB 06", -0x48), ReadDataNodeStub, (void**)&g_origReadDataNode);
#elif IS_RDR3
		MH_CreateHook(hook::get_pattern("40 8A BC 43", -0x3D), ReadDataNodeStub, (void**)&g_origReadDataNode);
#endif

#ifdef GTA_FIVE
		{
			auto floc = hook::get_pattern<char>("84 C0 0F 84 39 01 00 00 48 83 7F", -0x29);
			hook::set_call(&g_origCallSkip, floc + 0xD9);
			hook::call(floc + 0xD9, SkipCopyIf1s);
			MH_CreateHook(floc, UpdateSyncDataOn108Stub, (void**)&g_origUpdateSyncDataOn108);
		}
#endif

#ifdef GTA_FIVE
		MH_CreateHook(hook::get_pattern("48 83 79 48 00 48 8B D9 74 19", -6), ManuallyDirtyNodeStub, (void**)&g_origManuallyDirtyNode);
		MH_CreateHook(hook::get_pattern("85 51 28 0F 84 E4 00 00 00 33 DB", -0x24), netSyncDataNode_ForceSendStub, (void**)&g_orig_netSyncDataNode_ForceSend);
		MH_CreateHook(hook::get_pattern("44 85 41 28 74 73 83 79 30 00", -0x1F), netSyncDataNode_ForceSendToPlayerStub, (void**)&g_orig_netSyncDataNode_ForceSendToPlayer);
#elif IS_RDR3
		// for 1207: MH_CreateHook(hook::get_pattern("48 8B 03 48 8B CB 8B 7F 3C FF 50 20 80", -0x27), ManuallyDirtyNodeStub, (void**)&g_origManuallyDirtyNode);
		MH_CreateHook(hook::get_pattern("FF 50 20 8B 57 3C 48 8B C8 E8", -0x29), ManuallyDirtyNodeStub, (void**)&g_origManuallyDirtyNode);
		MH_CreateHook(hook::get_pattern("85 51 28 0F 84 ? ? ? ? 33 DB 39", -0x24), netSyncDataNode_ForceSendStub, (void**)&g_orig_netSyncDataNode_ForceSend);
		MH_CreateHook(hook::get_pattern("44 85 41 28 0F 84 ? ? ? ? 83", -0x18), netSyncDataNode_ForceSendToPlayerStub, (void**)&g_orig_netSyncDataNode_ForceSendToPlayer);
#endif

		MH_EnableHook(MH_ALL_HOOKS);
	}
});

#include <mmsystem.h>

static hook::cdecl_stub<void*()> _getConnectionManager([]()
{
#ifdef GTA_FIVE
	return hook::get_call(hook::get_pattern("E8 ? ? ? ? C7 44 24 40 60 EA 00 00"));
#elif IS_RDR3
	return hook::get_call(hook::get_pattern("48 8B D0 8B 46 ? 44 8B CD C7", -13));
#endif
});

static bool (*g_origInitializeTime)(void* timeSync, void* connectionMgr, int flags, void* trustHost,
	uint32_t sessionSeed, int* deltaStart, int packetFlags, int initialBackoff, int maxBackoff);

static bool g_initedTimeSync;

struct EmptyStruct
{

};

#ifdef GTA_FIVE
struct ExtraAddressData
{
	uint32_t addr;
	uint16_t port;
};

template<bool Enable>
using ExtraAddress = std::conditional_t<Enable, ExtraAddressData, EmptyStruct>;

template<int Build>
class netTimeSync
{
public:
	void Update()
	{
		if (!icgi->OneSyncEnabled)
		{
			return;
		}

		if (/*m_connectionMgr /*&& m_flags & 2 && */ !m_disabled)
		{
			uint32_t curTime = timeGetTime();

			if (!m_nextSync || int32_t(timeGetTime() - m_nextSync) >= 0)
			{
				m_requestSequence++;

				net::Buffer outBuffer;
				outBuffer.Write<uint32_t>(curTime); // request time
				outBuffer.Write<uint32_t>(m_requestSequence); // request sequence

				g_netLibrary->SendReliableCommand("msgTimeSyncReq", (const char*)outBuffer.GetData().data(), outBuffer.GetCurOffset());

				m_nextSync = (curTime + m_effectiveTimeBetweenSyncs) | 1;
			}
		}
	}

	void HandleTimeSync(net::Buffer& buffer)
	{
		auto reqTime = buffer.Read<uint32_t>();
		auto reqSequence = buffer.Read<uint32_t>();
		auto resDelta = buffer.Read<uint32_t>();

		if (m_disabled)
		{
			return;
		}

		/*if (!(m_flags & 2))
		{
			return;
		}*/

		// out of order?
		if (int32_t(reqSequence - m_replySequence) <= 0)
		{
			return;
		}

		auto rtt = timeGetTime() - reqTime;

		// bad timestamp, negative time passed
		if (int32_t(rtt) <= 0)
		{
			return;
		}

		int32_t timeDelta = resDelta + (rtt / 2) - timeGetTime();

		// is this a low RTT, or did we retry often enough?
		if (rtt <= 300 || m_retryCount >= 10)
		{
			if (!m_lastRtt)
			{
				m_lastRtt = rtt;
			}

			// is RTT within variance, low, or retried?
			if (rtt <= 100 || (rtt / m_lastRtt) < 2 || m_retryCount >= 10)
			{
				m_timeDelta = timeDelta;
				m_replySequence = reqSequence;

				// progressive backoff once we've established a valid time base
				if (m_effectiveTimeBetweenSyncs < m_configMaxBackoff)
				{
					m_effectiveTimeBetweenSyncs = std::min(m_configMaxBackoff, m_effectiveTimeBetweenSyncs * 2);
				}

				m_retryCount = 0;

				// use flag 4 to reset time at least once, even if game session code has done so to a higher value
				if (!(m_applyFlags & 4))
				{
					m_lastTime = m_timeDelta + timeGetTime();
				}

				m_applyFlags |= 7;
			}
			else
			{
				m_nextSync = 0;
				m_retryCount++;
			}

			// update average RTT
			m_lastRtt = (rtt + m_lastRtt) / 2;
		}
		else
		{
			m_nextSync = 0;
			m_retryCount++;
		}
	}

	bool IsInitialized()
	{
		if (!g_initedTimeSync)
		{
			g_origInitializeTime(this, _getConnectionManager(), 1, nullptr, 0, nullptr, 7, 2000, 60000);

			// to make the game not try to get time from us
			m_connectionMgr = nullptr;

			g_initedTimeSync = true;

			return false;
		}

		return (m_applyFlags & 4) != 0;
	}

	inline void SetConnectionManager(void* mgr)
	{
		m_connectionMgr = mgr;
	}

private:
	void* m_vtbl; // 0
	void* m_connectionMgr; // 8
	struct
	{
		uint32_t m_int1;
		uint16_t m_short1;
		uint32_t m_int2;
		uint16_t m_short2;
		ExtraAddress<(Build >= 2060)> m_addr3;
	} m_trustAddr; // 16
	uint32_t m_sessionKey; // 32
	int32_t m_timeDelta; // 36
	struct
	{
		void* self;
		void* cb;
	} messageDelegate; // 40
	char m_pad_38[32]; // 56
	uint32_t m_nextSync; // 88
	uint32_t m_configTimeBetweenSyncs; // 92
	uint32_t m_configMaxBackoff; // 96, usually 60000
	uint32_t m_effectiveTimeBetweenSyncs; // 100
	uint32_t m_lastRtt; // 104
	uint32_t m_retryCount; // 108
	uint32_t m_requestSequence; // 112
	uint32_t m_replySequence; // 116
	uint32_t m_flags; // 120
	uint32_t m_packetFlags; // 124
	uint32_t m_lastTime; // 128, used to prevent time from going backwards
	uint8_t m_applyFlags; // 132
	uint8_t m_disabled; // 133
};
#elif IS_RDR3
struct ExtraPaddingData
{
	char extra_padding[24];
};

template<bool Enable>
using ExtraPadding = std::conditional_t<Enable, ExtraPaddingData, EmptyStruct>;


template<int Build>
class netTimeSync
{
public:
	void Update()
	{
		if (!icgi->OneSyncEnabled)
		{
			return;
		}

		if (/*m_connectionMgr /*&& m_flags & 2 && */ !m_disabled)
		{
			uint32_t curTime = timeGetTime();

			if (!m_nextSync || int32_t(timeGetTime() - m_nextSync) >= 0)
			{
				m_requestSequence++;

				net::Buffer outBuffer;
				outBuffer.Write<uint32_t>(curTime); // request time
				outBuffer.Write<uint32_t>(m_requestSequence); // request sequence

				g_netLibrary->SendReliableCommand("msgTimeSyncReq", (const char*)outBuffer.GetData().data(), outBuffer.GetCurOffset());

				m_nextSync = (curTime + m_effectiveTimeBetweenSyncs) | 1;
			}
		}
	}

	void HandleTimeSync(net::Buffer& buffer)
	{
		auto reqTime = buffer.Read<uint32_t>();
		auto reqSequence = buffer.Read<uint32_t>();
		auto resDelta = buffer.Read<uint32_t>();

		if (m_disabled)
		{
			return;
		}

		/*if (!(m_flags & 2))
		{
			return;
		}*/

		// out of order?
		if (int32_t(reqSequence - m_replySequence) <= 0)
		{
			return;
		}

		auto rtt = timeGetTime() - reqTime;

		// bad timestamp, negative time passed
		if (int32_t(rtt) <= 0)
		{
			return;
		}

		int32_t timeDelta = resDelta + (rtt / 2) - timeGetTime();

		// is this a low RTT, or did we retry often enough?
		if (rtt <= 300 || m_retryCount >= 10)
		{
			if (!m_lastRtt)
			{
				m_lastRtt = rtt;
			}

			// is RTT within variance, low, or retried?
			if (rtt <= 100 || (rtt / m_lastRtt) < 2 || m_retryCount >= 10)
			{
				m_timeDelta = timeDelta;
				m_replySequence = reqSequence;

				// progressive backoff once we've established a valid time base
				if (m_effectiveTimeBetweenSyncs < m_configMaxBackoff)
				{
					m_effectiveTimeBetweenSyncs = std::min(m_configMaxBackoff, m_effectiveTimeBetweenSyncs * 2);
				}

				m_retryCount = 0;

				// use flag 4 to reset time at least once, even if game session code has done so to a higher value
				if (!(m_applyFlags & 4))
				{
					m_lastTime = m_timeDelta + timeGetTime();
				}

				m_applyFlags |= 7;
			}
			else
			{
				m_nextSync = 0;
				m_retryCount++;
			}

			// update average RTT
			m_lastRtt = (rtt + m_lastRtt) / 2;
		}
		else
		{
			m_nextSync = 0;
			m_retryCount++;
		}
	}

	bool IsInitialized()
	{
		if (!g_initedTimeSync)
		{
			g_origInitializeTime(this, _getConnectionManager(), 1, nullptr, 0, nullptr, 7, 2000, 60000);

			// to make the game not try to get time from us
			m_connectionMgr = nullptr;

			// we don't want to use cloud time
			m_useCloudTime = false;

			g_initedTimeSync = true;

			return false;
		}

		return (m_applyFlags & 4) != 0;
	}

	inline void SetConnectionManager(void* mgr)
	{
		m_connectionMgr = mgr;
	}

public:
	void* m_vtbl; // +0
	void* m_connectionMgr; // +8
	uint32_t m_unkTrust; // +16
	uint32_t m_sessionKey; // +20
	char m_pad_24[44]; // +24
	ExtraPadding<(Build >= 1355)> m_pad_72;
	uint32_t m_nextSync; // +72
	uint32_t m_configTimeBetweenSyncs; // +76
	uint32_t m_configMaxBackoff; // +80, usually 60000
	uint32_t m_effectiveTimeBetweenSyncs; // +84
	uint32_t m_lastRtt; // +88
	uint32_t m_retryCount; // +92
	uint32_t m_requestSequence; // +96
	uint32_t m_replySequence; // +100
	uint32_t m_flags; // +104
	uint32_t m_packetFlags; // +108
	int32_t m_timeDelta; // +112
	char m_pad_116[28];
	uint32_t m_lastTime; // +144, used to prevent time from going backwards
	uint8_t m_applyFlags; // +148
	uint8_t m_unk5; // +149
	uint8_t m_disabled; // +150
	uint8_t m_useCloudTime; // +151
};
#endif

template<int Build>
static netTimeSync<Build>** g_netTimeSync;

template<int Build>
bool netTimeSync__InitializeTimeStub(netTimeSync<Build>* timeSync, void* connectionMgr, int flags, void* trustHost,
	uint32_t sessionSeed, int* deltaStart, int packetFlags, int initialBackoff, int maxBackoff)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origInitializeTime(timeSync, connectionMgr, flags, trustHost, sessionSeed, deltaStart, packetFlags, initialBackoff, maxBackoff);
	}

	timeSync->SetConnectionManager(connectionMgr);

	return true;
}

bool IsWaitingForTimeSync()
{
#ifdef GTA_FIVE
	if (xbr::IsGameBuildOrGreater<2060>())
	{
		return !(*g_netTimeSync<2060>)->IsInitialized();
	}

	return !(*g_netTimeSync<1604>)->IsInitialized();
#elif IS_RDR3
	if (xbr::IsGameBuildOrGreater<1355>())
	{
		return !(*g_netTimeSync<1355>)->IsInitialized();
	}

	return !(*g_netTimeSync<1311>)->IsInitialized();
#endif
}

static InitFunction initFunctionTime([]()
{
	NetLibrary::OnNetLibraryCreate.Connect([](NetLibrary* lib)
	{
		lib->AddReliableHandler("msgTimeSync", [](const char* data, size_t len)
		{
			net::Buffer buf(reinterpret_cast<const uint8_t*>(data), len);

#ifdef GTA_FIVE
			if (xbr::IsGameBuildOrGreater<2060>())
			{
				(*g_netTimeSync<2060>)->HandleTimeSync(buf);
			}
			else
			{
				(*g_netTimeSync<1604>)->HandleTimeSync(buf);
			}
#elif IS_RDR3
			if (xbr::IsGameBuildOrGreater<1355>())
			{
				(*g_netTimeSync<1355>)->HandleTimeSync(buf);
			}
			else
			{
				(*g_netTimeSync<1311>)->HandleTimeSync(buf);
			}
#endif
		});
	});
});

static HookFunction hookFunctionTime([]()
{
	MH_Initialize();

#ifdef GTA_FIVE
	void* func = (xbr::IsGameBuildOrGreater<2060>()) ? (void*)&netTimeSync__InitializeTimeStub<2060> : &netTimeSync__InitializeTimeStub<1604>;
	MH_CreateHook(hook::get_pattern("48 8B D9 48 39 79 08 0F 85 ? ? 00 00 41 8B E8", -32), func, (void**)&g_origInitializeTime);
#elif IS_RDR3
	void* func = (xbr::IsGameBuildOrGreater<1355>()) ? (void*)&netTimeSync__InitializeTimeStub<1355> : &netTimeSync__InitializeTimeStub<1311>;
	MH_CreateHook(hook::get_pattern("48 89 51 08 41 83 F8 02 44 0F 45 C8", -49), func, (void**)&g_origInitializeTime);
#endif

	MH_EnableHook(MH_ALL_HOOKS);

#ifdef GTA_FIVE
	if (xbr::IsGameBuildOrGreater<2060>())
	{
		g_netTimeSync<2060> = hook::get_address<netTimeSync<2060>**>(hook::get_pattern("48 8B 0D ? ? ? ? 45 33 C9 45 33 C0 41 8D 51 01 E8", 3));
	}
	else
	{
		g_netTimeSync<1604> = hook::get_address<netTimeSync<1604>**>(hook::get_pattern("EB 16 48 8B 0D ? ? ? ? 45 33 C9 45 33 C0", 5));
	}
#elif IS_RDR3
	auto location = hook::get_pattern("4C 8D 45 50 41 03 C7 44 89 6D 50 89", -4);

	if (xbr::IsGameBuildOrGreater<1355>())
	{
		g_netTimeSync<1355> = hook::get_address<netTimeSync<1355>**>(location);
	}
	else
	{
		g_netTimeSync<1311> = hook::get_address<netTimeSync<1311>**>(location);
	}
#endif

	OnMainGameFrame.Connect([]()
	{
#if GTA_FIVE
		if (xbr::IsGameBuildOrGreater<2060>())
		{
			(*g_netTimeSync<2060>)->Update();
		}
		else
		{
			(*g_netTimeSync<1604>)->Update();
		}
#elif IS_RDR3
		if (xbr::IsGameBuildOrGreater<1355>())
		{
			(*g_netTimeSync<1355>)->Update();
		}
		else
		{
			(*g_netTimeSync<1311>)->Update();
		}
#endif
	});
});

template<typename TIndex>
struct WorldGridEntry
{
	uint8_t sectorX;
	uint8_t sectorY;
	TIndex slotID;
};

template<typename TIndex, int TCount>
struct WorldGridState
{
	WorldGridEntry<TIndex> entries[TCount];
};

static WorldGridState<uint8_t, 12> g_worldGrid[256];
static WorldGridState<uint16_t, 32> g_worldGrid2[1];

static InitFunction initFunctionWorldGrid([]()
{
	NetLibrary::OnNetLibraryCreate.Connect([](NetLibrary* lib)
	{
		lib->AddReliableHandler("msgWorldGrid", [](const char* data, size_t len)
		{
			net::Buffer buf(reinterpret_cast<const uint8_t*>(data), len);
			auto base = buf.Read<uint16_t>();
			auto length = buf.Read<uint16_t>();

			if ((base + length) > sizeof(g_worldGrid))
			{
				return;
			}

			buf.Read(reinterpret_cast<char*>(g_worldGrid) + base, length);
		});

		lib->AddReliableHandler("msgWorldGrid3", [](const char* data, size_t len)
		{
			net::Buffer buf(reinterpret_cast<const uint8_t*>(data), len);
			auto base = buf.Read<uint32_t>();
			auto length = buf.Read<uint32_t>();

			if ((size_t(base) + length) > sizeof(g_worldGrid2))
			{
				return;
			}

			buf.Read(reinterpret_cast<char*>(g_worldGrid2) + base, length);
		});
	});
});

bool(*g_origDoesLocalPlayerOwnWorldGrid)(float* pos);

namespace sync
{
extern void* origin;
extern float* (*getCoordsFromOrigin)(void*, float*);
}

bool DoesLocalPlayerOwnWorldGrid(float* pos)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origDoesLocalPlayerOwnWorldGrid(pos);
	}

	alignas(16) float centerOfWorld[4];
	sync::getCoordsFromOrigin(sync::origin, centerOfWorld);

	float dX = pos[0] - centerOfWorld[0];
	float dY = pos[1] - centerOfWorld[1];

	// #TODO1S: make server able to send current range for player (and a world grid granularity?)
	constexpr float maxRange = (424.0f * 424.0f);

	if (icgi->NetProtoVersion < 0x202007021121)
	{
		int sectorX = std::max(pos[0] + 8192.0f, 0.0f) / 75;
		int sectorY = std::max(pos[1] + 8192.0f, 0.0f) / 75;

		auto playerIdx = g_playerMgr->localPlayer->physicalPlayerIndex();

		bool does = false;

		for (const auto& entry : g_worldGrid[playerIdx].entries)
		{
			if (entry.sectorX == sectorX && entry.sectorY == sectorY && entry.slotID == playerIdx)
			{
				does = true;
				break;
			}
		}

		return does;
	}
	else
	{
		int sectorX = std::max(pos[0] + 8192.0f, 0.0f) / 150;
		int sectorY = std::max(pos[1] + 8192.0f, 0.0f) / 150;

		auto playerIdx = g_netIdsByPlayer[g_playerMgr->localPlayer];

		bool does = false;

		for (const auto& entry : g_worldGrid2[0].entries)
		{
			if (entry.sectorX == sectorX && entry.sectorY == sectorY && entry.slotID == playerIdx)
			{
				does = true;
				break;
			}
		}

		// if the entity would be created outside of culling range, suppress it
		if (((dX * dX) + (dY * dY)) > maxRange)
		{
			if (does)
			{
				does = false;
			}
		}

		return does;
	}
}

static int GetScriptParticipantIndexForPlayer(CNetGamePlayer* player)
{
	return player->physicalPlayerIndex();
}

#ifdef GTA_FIVE
static int DoesLocalPlayerOwnWorldGridWrapForInline(float* pos)
{
	return (DoesLocalPlayerOwnWorldGrid(pos)) ? 0 : 1;
}

static int* dword_1427EA288;
static int* dword_141D56E08;

static hook::cdecl_stub<float(void*, void*, void*, void*, void*, void*, void*)> _countPopVehiclesForSpace([]()
{
	return hook::get_call(hook::get_pattern("40 38 3D ? ? ? ? F3 0F 2C C0 89", -5));
});

static hook::cdecl_stub<bool(int peds, int cars, int n5s, int n8s, int n3s, bool)> _checkForObjectSpace([]()
{
	return hook::get_call(hook::get_pattern("45 33 C9 45 33 C0 33 C9 40 88", 17));
});

static bool PopulationPedCheck(int num)
{
	int count = (int)_countPopVehiclesForSpace(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) - *dword_1427EA288;

	if (count > 0)
	{
		return _checkForObjectSpace(num, 0, 0, 0, 0, false);
	}

	return true;
}
#endif

static HookFunction hookFunctionWorldGrid([]()
{
#ifdef GTA_FIVE
	// inline turntaking for ped creation
	{
		auto location = hook::get_pattern<char>("45 33 C9 44 88 74 24 20 E8 ? ? ? ? 44 8B E0", 8);
		hook::call(location, DoesLocalPlayerOwnWorldGridWrapForInline);

		// if '1', instantly fail, don't try to iterate that one player
		hook::jump(location + 12 + 4, location - 0x16D);

		// precede it with moving the iterator back in `edx`, or this will loop for a *long* time sometimes
		hook::put<uint32_t>(location + 12, 0x6424548B);
	}

	// second variant of the same
	{
		auto location = hook::get_pattern<char>("48 8B CE 44 88 6C 24 20 E8 ? ? ? ? 45 8B FD", 8);
		hook::call(location, DoesLocalPlayerOwnWorldGridWrapForInline);

		// if '1', instantly fail, don't try to iterate that one player
		hook::jump(location + 15, location - 0xD5);
	}

	// turntaking
	hook::jump(hook::get_pattern("48 8D 4C 24 30 45 33 C9 C6", -0x30), DoesLocalPlayerOwnWorldGrid);

	hook::jump(hook::get_pattern(((xbr::IsGameBuildOrGreater<2060>()) ? "BE 01 00 00 00 8B E8 85 C0 0F 84 B8" : "BE 01 00 00 00 45 33 C9 40 88 74 24 20"), ((xbr::IsGameBuildOrGreater<2060>()) ? -0x3A : -0x2D)), DoesLocalPlayerOwnWorldGrid);
#endif

	MH_Initialize();

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("44 8A 40 ? 41 80 F8 FF 0F", -0x1B), DoesLocalPlayerOwnWorldGrid, (void**)&g_origDoesLocalPlayerOwnWorldGrid);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("44 0F B6 C0 F3 0F 5F C3 41 0F B6 04 10", -0x35), DoesLocalPlayerOwnWorldGrid, (void**)&g_origDoesLocalPlayerOwnWorldGrid);
#endif

	MH_EnableHook(MH_ALL_HOOKS);

	// if population breaks in non-1s, this is possibly why
	//hook::nop(hook::get_pattern("38 05 ? ? ? ? 75 0A 48 8B CF E8", 6), 2);

	// 1493+ of the above patch
	//hook::nop(hook::get_pattern("80 3D ? ? ? ? 00 75 0A 48 8B CF E8", 7), 2);

	// this patch above ^ shouldn't be needed with timeSync properly implemented, gamerIDs being set and RemotePlayer list fixes

	// this should apply to both 1s and non-1s (as participants are - hopefully? - not used by anyone in regular net)
	hook::jump(hook::get_pattern("84 C0 74 06 0F BF 43 38", -0x18), GetScriptParticipantIndexForPlayer);

#ifdef GTA_FIVE
	// don't add 'maybe enough to give all our vehicles drivers' as a constraint for even creating one ped
	// (applies to non-1s too: generally safe)
	hook::call(hook::get_pattern("0F 28 F1 74 15 8D 48 01 E8", 8), PopulationPedCheck);
	hook::call(hook::get_pattern("48 8B D9 74 4C B9 01 00 00 00 E8", 10), PopulationPedCheck);

	dword_1427EA288 = hook::get_address<int*>(hook::get_pattern("F3 0F 2C C0 2B 05 ? ? ? ? 85 C0 0F", 6));
	dword_141D56E08 = hook::get_address<int*>(hook::get_pattern("3B C7 0F 42 F8 83 3D ? ? ? ? 01 75", -4));

	// sometimes it seems to need this to be patched still - hope that won't fail on 2060+
	// b2060+ seem to have this obfuscated, sorry guys
	if (!xbr::IsGameBuildOrGreater<2060>())
	{
		hook::put<uint16_t>(hook::get_pattern("F3 0F 59 C8 F3 48 0F 2C C9 03 CF E8", 9), 0xF989);
	}
#endif
});

int ObjectToEntity(int objectId)
{
	int entityIdx = -1;
	auto object = TheClones->GetNetObject(objectId & 0xFFFF);

	if (object)
	{
		entityIdx = getScriptGuidForEntity(object->GetGameObject());
	}

	return entityIdx;
}

#include <boost/range/adaptor/map.hpp>
#include <mmsystem.h>

struct ObjectData
{
	uint32_t lastSyncTime;
	uint32_t lastSyncAck;

	ObjectData()
	{
		lastSyncTime = 0;
		lastSyncAck = 0;
	}
};

static std::map<int, ObjectData> trackedObjects;

#include <lz4.h>

#ifdef GTA_FIVE
extern void ArrayManager_Update();
#endif

static InitFunction initFunction([]()
{
	OnMainGameFrame.Connect([]()
	{
		if (g_netLibrary == nullptr)
		{
			return;
		}

		if (g_netLibrary->GetConnectionState() != NetLibrary::CS_ACTIVE)
		{
			return;
		}

		// protocol 5 or higher are aware of this state
		if (g_netLibrary->GetServerProtocol() <= 4)
		{
			return;
		}

		// only work with 1s enabled
		if (!icgi->OneSyncEnabled)
		{
			return;
		}

#ifdef GTA_FIVE
		ArrayManager_Update();
#endif

		EventManager_Update();
		TheClones->Update();
	});

	OnKillNetwork.Connect([](const char*)
	{
		g_events.clear();
		g_reEventQueue.clear();
	});

	OnKillNetworkDone.Connect([]()
	{
		trackedObjects.clear();
	});

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_ENTITY_OWNER", [](fx::ScriptContext& context)
	{
		fwEntity* entity = rage::fwScriptGuid::GetBaseFromGuid(context.GetArgument<int>(0));

		if (!entity)
		{
			context.SetResult<int>(-1);
			return;
		}

		rage::netObject* netObj = (rage::netObject*)entity->GetNetObject();

		if (!netObj)
		{
			context.SetResult<int>(-1);
			return;
		}

		auto owner = netObject__GetPlayerOwner(netObj);
		context.SetResult<int>(owner->physicalPlayerIndex());
	});

#ifdef ONESYNC_CLONING_NATIVES
	fx::ScriptEngine::RegisterNativeHandler("EXPERIMENTAL_SAVE_CLONE_CREATE", [](fx::ScriptContext& context)
	{
		char* entity = (char*)rage::fwScriptGuid::GetBaseFromGuid(context.GetArgument<int>(0));

		if (!entity)
		{
			trace("SAVE_CLONE_CREATE: invalid entity\n");

			context.SetResult<const char*>("");
			return;
		}

		auto netObj = *(rage::netObject**)(entity + g_entityNetObjOffset);

		static char blah[90000];

		static char bluh[1000];
		memset(bluh, 0, sizeof(bluh));
		memset(blah, 0, sizeof(blah));

		rage::datBitBuffer buffer(bluh, sizeof(bluh));

		auto st = netObj->GetSyncTree();
		st = rage::netSyncTree::GetForType((NetObjEntityType)netObj->GetObjectType());
		//st->Write(1, 0, netObj, &buffer, 0, 31, nullptr, nullptr);
		st->WriteTreeCfx(1, 0, netObj, &buffer, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp(), nullptr, 31, nullptr, nullptr);

		trace("create buffer length = %d\n", buffer.GetDataLength());

		static char base64Buffer[2000];
		size_t outLength = sizeof(base64Buffer);
		char* txt = base64_encode((uint8_t*)buffer.m_data, (buffer.m_curBit / 8) + 1, &outLength);

		memcpy(base64Buffer, txt, outLength);
		free(txt);

		base64Buffer[outLength] = '\0';

		context.SetResult<const char*>(base64Buffer);
	});

	fx::ScriptEngine::RegisterNativeHandler("EXPERIMENTAL_SAVE_CLONE_SYNC", [](fx::ScriptContext& context)
	{
		char* entity = (char*)rage::fwScriptGuid::GetBaseFromGuid(context.GetArgument<int>(0));

		if (!entity)
		{
			trace("SAVE_CLONE_SYNC: invalid entity\n");

			context.SetResult<const char*>("");
			return;
		}

		auto netObj = *(rage::netObject**)(entity + g_entityNetObjOffset);

		static char blah[90000];

		static char bluh[1000];
		memset(bluh, 0, sizeof(bluh));
		memset(blah, 0, sizeof(blah));

		rage::datBitBuffer buffer(bluh, sizeof(bluh));

		auto st = netObj->GetSyncTree();
		st = rage::netSyncTree::GetForType((NetObjEntityType)netObj->GetObjectType());
		//st->Write(2, 0, netObj, &buffer, 0, 31, nullptr, nullptr);
		st->WriteTreeCfx(2, 1, netObj, &buffer, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp(), nullptr, 31, nullptr, nullptr);

		static char base64Buffer[2000];

		size_t outLength = sizeof(base64Buffer);
		char* txt = base64_encode((uint8_t*)buffer.m_data, (buffer.m_curBit / 8) + 1, &outLength);

		memcpy(base64Buffer, txt, outLength);
		free(txt);

		// trace("saving netobj %llx\n", (uintptr_t)netObj);

		base64Buffer[outLength] = '\0';

		context.SetResult<const char*>(base64Buffer);
	});

	fx::ScriptEngine::RegisterNativeHandler("EXPERIMENTAL_LOAD_CLONE_CREATE", [](fx::ScriptContext& context)
	{
		auto data = context.GetArgument<const char*>(0);
		auto objectId = context.GetArgument<uint16_t>(1);
		auto objectType = context.GetArgument<const char*>(2);

		NetObjEntityType objType;

		if (strcmp(objectType, "automobile") == 0)
		{
			objType = NetObjEntityType::Automobile;
		}
		else if (strcmp(objectType, "boat") == 0)
		{
			objType = NetObjEntityType::Boat;
		}
		else if (strcmp(objectType, "trailer") == 0)
		{
			objType = NetObjEntityType::Trailer;
		}
#ifdef IS_RDR3
		else if (strcmp(objectType, "animal") == 0)
		{
			objType = NetObjEntityType::Animal;
		}
		else if (strcmp(objectType, "draftveh") == 0)
		{
			objType = NetObjEntityType::DraftVeh;
		}
		else if (strcmp(objectType, "horse") == 0)
		{
			objType = NetObjEntityType::Horse;
		}
#endif
		else if (strcmp(objectType, "train") == 0)
		{
			objType = NetObjEntityType::Train;
		}
		else if (strcmp(objectType, "player") == 0)
		{
			objType = NetObjEntityType::Player; // until we make native players
		}
		else if (strcmp(objectType, "ped") == 0)
		{
			objType = NetObjEntityType::Ped;
		}
		else
		{
			context.SetResult(-1);
			return;
		}

		//trace("making a %d\n", (int)objType);

		size_t decLen;
		uint8_t* dec = base64_decode(data, strlen(data), &decLen);

		rage::datBitBuffer buf(dec, decLen);
		buf.m_f1C = 1;

		auto st = rage::netSyncTree::GetForType(objType);

		st->ReadFromBuffer(1, 0, &buf, nullptr);

		free(dec);

		auto obj = rage::CreateCloneObject(objType, objectId, 31, 0, 32);
		obj->syncData.isRemote = true;

		if (!st->CanApplyToObject(obj))
		{
			trace("Couldn't apply object.\n");

			delete obj;

			context.SetResult(-1);
			return;
		}

#ifdef IS_RDR3
		auto check2 = (*(unsigned __int8(__fastcall**)(void*))(*(uint64_t*)obj + 0x98))(obj);
		auto check = st->m_18(obj, -1);

		if (!check && !check2)
		{
			trace("Can't apply\n");
			return;
		}

		(*(void(__fastcall**)(void*))(*(uint64_t*)obj + 0x248))(obj);
#endif

		st->ApplyToObject(obj, nullptr);
		rage::netObjectMgr::GetInstance()->RegisterNetworkObject(obj);

		// create a blender, if not existent
		if (!obj->GetBlender())
		{
			trace("Created net blender\n");
			obj->CreateNetBlender();
		}

		if (!obj->GetBlender())
		{
			trace("No blender even after creation, that's bad!\n");

			context.SetResult(-1);
			return;
		}

		(*(void(__fastcall**)(void*, uint32_t))(*(uint64_t*)obj + 0x260))(obj, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp());

		obj->GetBlender()->ApplyBlend();

		//obj->GetBlender()->SetTimestamp(rage::netInterface_queryFunctions::GetInstance()->GetTimestamp());
		//obj->SetBlenderTimestamp(rage::netInterface_queryFunctions::GetInstance()->GetTimestamp());
#ifdef IS_RDR3
		(*(void(__fastcall**)(void*))(*(uint64_t*)obj + 0x250))(obj);

		if ((*(unsigned __int8(__fastcall**)(void*))(*(uint64_t*)obj + 0x280))(obj))
		{
			obj->GetBlender()->m_70();
		}

		obj->GetBlender()->ApplyBlend();

		obj->GetBlender()->m_38();

		(*(void(__fastcall**)(void*))(*(uint64_t*)obj + 0x240))(obj);
#endif

#if 0
		if ((*(unsigned __int8(__fastcall**)(void*))(*(uint64_t*)obj + 0x288))(obj))
		{
			auto v156 = (*(__int64(__fastcall**)(void*))(*(uint64_t*)obj + 0xB0))(obj);
			if (v156)
			{
				auto v159 = (*(__int64(__fastcall**)(void*))(*(uint64_t*)obj + 0xB0))(obj);
				*(DWORD*)(v159 + 144) &= 0xFFFFF7FF;
				auto v160 = (*(__int64(__fastcall**)(void*))(*(uint64_t*)obj + 0xB0))(obj);
				*(DWORD*)(v160 + 144) |= 0x400u;
			}
		}
#endif

		context.SetResult(getScriptGuidForEntity(obj->GetGameObject()));
	});

	fx::ScriptEngine::RegisterNativeHandler("EXPERIMENTAL_LOAD_CLONE_SYNC", [](fx::ScriptContext& context)
	{
		char* entity = (char*)rage::fwScriptGuid::GetBaseFromGuid(context.GetArgument<int>(0));

		if (!entity)
		{
			trace("LOAD_CLONE_SYNC: invalid entity\n");
			return;
		}

		auto obj = *(rage::netObject**)(entity + g_entityNetObjOffset);

		auto data = context.GetArgument<const char*>(1);

		size_t decLen;
		uint8_t* dec = base64_decode(data, strlen(data), &decLen);

		rage::datBitBuffer buf(dec, decLen);
		buf.m_f1C = 1;

		auto st = obj->GetSyncTree();

		st->ReadFromBuffer(2, 0, &buf, nullptr);

		free(dec);

		if (!obj->GetBlender())
		{
			trace("LOAD_CLONE_SYNC: no net blender\n");
			return;
		}

#if 0
		(*(void(__fastcall**)(void*, uint32_t))(*(uint64_t*)obj + 0x260))(obj, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp());
		obj->GetBlender()->m_28();

		if (*(uint8_t*)((char*)obj + 84))
		{
			auto init = st->InitialiseTree();

			(*(void(__fastcall**)(void*, uint8_t, void*, void*, void*))(*(uint64_t*)st + 0x48))(
				st,
				2,
				obj,
				(void*)init,
				obj + 1056);
		}
		else
		{

			(*(void(__fastcall**)(void*))(*(uint64_t*)obj + 0x248))(obj);
			st->ApplyToObject(obj, nullptr);
			(*(void(__fastcall**)(void*))(*(uint64_t*)obj + 0x250))(obj);
			//trace("object = %d\n", (uint64_t)obj);
		}
#endif
		(*(void(__fastcall**)(void*, uint32_t))(*(uint64_t*)obj + 0x260))(obj, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp());
		//(*(void(__fastcall**)(void*, uint32_t))(*(uint64_t*)(obj->GetBlender()) + 0x38))(obj->GetBlender(), 0);

		(*(void(__fastcall**)(void*))(*(uint64_t*)obj + 0x248))(obj);
		st->ApplyToObject(obj, nullptr);
		(*(void(__fastcall**)(void*))(*(uint64_t*)obj + 0x250))(obj);

		//obj->GetBlender()->m_30();
		//obj->GetBlender()->m_58();

		//obj->GetBlender()->ApplyBlend();
		//obj->GetBlender()->m_38();

		//obj->m_1C0();
	});

	fx::ScriptEngine::RegisterNativeHandler("EXPERIMENTAL_BLEND", [](fx::ScriptContext& context)
	{
		char* entity = (char*)rage::fwScriptGuid::GetBaseFromGuid(context.GetArgument<int>(0));

		if (!entity)
		{
			trace("LOAD_CLONE_SYNC: invalid entity\n");
			return;
		}

		auto obj = *(rage::netObject**)(entity + g_entityNetObjOffset);
		//obj->GetBlender()->m_30();
		obj->GetBlender()->m_58();
	});

	static ConsoleCommand saveCloneCmd("save_clone", [](const std::string& address)
	{
		uintptr_t addressPtr = _strtoui64(address.c_str(), nullptr, 16);
		auto netObj = *(rage::netObject**)(addressPtr + g_entityNetObjOffset);

		static char blah[90000];

		static char bluh[1000];

		rage::datBitBuffer buffer(bluh, sizeof(bluh));

		auto st = netObj->GetSyncTree();
		//st->Write(1, rage::netInterface_queryFunctions::GetInstance()->GetTimestamp(), netObj, &buffer, 0, 31, nullptr, nullptr);

		FILE* f = _wfopen(MakeRelativeCitPath(L"tree.bin").c_str(), L"wb");
		fwrite(buffer.m_data, 1, (buffer.m_curBit / 8) + 1, f);
		fclose(f);
	});

	static ConsoleCommand loadCloneCmd("load_clone", []()
	{
		FILE* f = _wfopen(MakeRelativeCitPath(L"tree.bin").c_str(), L"rb");
		fseek(f, 0, SEEK_END);

		int len = ftell(f);

		fseek(f, 0, SEEK_SET);

		uint8_t data[1000];
		fread(data, 1, 1000, f);

		fclose(f);

		rage::datBitBuffer buf(data, len);
		buf.m_f1C = 1;

		auto st = rage::netSyncTree::GetForType(NetObjEntityType::Ped);

		st->ReadFromBuffer(1, 0, &buf, nullptr);

		auto obj = rage::CreateCloneObject(NetObjEntityType::Ped, rand(), 31, 0, 32);

		if (!st->CanApplyToObject(obj))
		{
			trace("Couldn't apply object.\n");

			delete obj;
			return;
		}

		st->ApplyToObject(obj, nullptr);
		rage::netObjectMgr::GetInstance()->RegisterNetworkObject(obj);

		obj->GetBlender()->SetTimestamp(rage::netInterface_queryFunctions::GetInstance()->GetTimestamp());
		//obj->SetBlenderTimestamp(rage::netInterface_queryFunctions::GetInstance()->GetTimestamp());

		obj->m_1D0();

		obj->GetBlender()->ApplyBlend();
		obj->GetBlender()->m_38();

		obj->m_1C0();

		trace("got game object %llx\n", (uintptr_t)obj->GetGameObject());
	});
#endif
});

uint32_t* rage__s_NetworkTimeLastFrameStart;
uint32_t* rage__s_NetworkTimeThisFrameStart;

static void (*g_origSendCloneSync)(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6);

static void SendCloneSync(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6)
{
	auto t = *rage__s_NetworkTimeThisFrameStart;
	*rage__s_NetworkTimeThisFrameStart = *rage__s_NetworkTimeLastFrameStart;

	g_origSendCloneSync(a1, a2, a3, a4, a5, a6);

	*rage__s_NetworkTimeThisFrameStart = t;
}

static HookFunction hookFunctionNative([]()
{
	MH_Initialize();

#ifdef GTA_FIVE
	MH_CreateHook(hook::get_pattern("41 56 41 57 48 83 EC 40 0F B6 72 ? 4D 8B E1", -0x14), SendCloneSync, (void**)&g_origSendCloneSync);
#elif IS_RDR3
	MH_CreateHook(hook::get_pattern("48 8B 06 41 B0 01 41 0F B6 5E", -0x50), SendCloneSync, (void**)&g_origSendCloneSync);
#endif

	MH_EnableHook(MH_ALL_HOOKS);

#ifdef GTA_FIVE
	rage__s_NetworkTimeThisFrameStart = hook::get_address<uint32_t*>(hook::get_pattern("49 8B 0F 40 8A D6 41 2B C4 44 3B 25", 12));
	rage__s_NetworkTimeLastFrameStart = hook::get_address<uint32_t*>(hook::get_pattern("89 05 ? ? ? ? 48 8B 01 FF 50 10 80 3D", 2));
#elif IS_RDR3
	rage__s_NetworkTimeThisFrameStart = hook::get_address<uint32_t*>(hook::get_pattern("74 ? 8B 05 ? ? ? ? 48 8B 0D ? ? ? ? 89", 4));
	rage__s_NetworkTimeLastFrameStart = hook::get_address<uint32_t*>(hook::get_pattern("89 05 ? ? ? ? 48 8B 01 FF 50 ? 80 3D", 2));
#endif
});

#ifdef GTA_FIVE
static hook::cdecl_stub<const char*(int, uint32_t)> rage__atHashStringNamespaceSupport__GetString([]
{
	return hook::get_pattern("85 D2 75 04 33 C0 EB 61", -0x18);
});

#include <Streaming.h>

struct CGameScriptId
{
	void* vtbl;
	uint32_t hash;
	char scriptName[32];
};

static hook::cdecl_stub<CGameScriptId*(rage::fwEntity*)> _getEntityScript([]()
{
	return hook::get_pattern("74 2A 48 8D 58 08 48 8B CB 48 8B 03", -0x18);
});

static auto PoolDiagDump(const char* poolName)
{
	auto pool = rage::GetPoolBase(poolName);

	std::map<std::string, int> poolCount;

	for (int i = 0; i < pool->GetSize(); i++)
	{
		auto e = pool->GetAt<rage::fwEntity>(i);

		if (e)
		{
			std::string desc;
			auto archetype = e->GetArchetype();
			auto archetypeHash = archetype->hash;
			auto archetypeName = streaming::GetStreamingBaseNameForHash(archetypeHash);

			if (archetypeName.empty())
			{
				archetypeName = va("0x%08x", archetypeHash);
			}

			desc = fmt::sprintf("%s", archetypeName);

			auto script = _getEntityScript(e);
			if (script)
			{
				desc += fmt::sprintf(" (script %s)", script->scriptName);
			}

			if (!e->GetNetObject())
			{
				desc += " (no netobj)";
			}

			poolCount[desc]++;
		}
	}

	std::vector<std::pair<int, std::string>> entries;

	for (const auto& [type, count] : poolCount)
	{
		entries.push_back({ count, type });
	}

	std::sort(entries.begin(), entries.end(), [](const auto& l, const auto& r)
	{
		return r.first < l.first;
	});

	std::string poolSummary;

	for (const auto& [count, type] : entries)
	{
		poolSummary += fmt::sprintf("  %s: %d entries\n", type, count);
	}

	return poolSummary;
}

static void PedPoolDiagError()
{
	FatalError("Ran out of ped pool space while creating network object.\n\nPed pool dump:\n%s", PoolDiagDump("Peds"));
}

static HookFunction hookFunctionDiag([]
{
	// CreatePed
	hook::call(hook::get_pattern("75 0E B1 01 E8 ? ? ? ? 33 C9 E8 ? ? ? ? 48 8B 03", 11), PedPoolDiagError);

	// CreatePlayer
	hook::call(hook::get_pattern("75 0E B1 01 E8 ? ? ? ? 33 C9 E8 ? ? ? ? 4C 8B 03", 11), PedPoolDiagError);

#if _DEBUG
	static ConsoleCommand pedCrashCmd("ped_crash", []()
	{
		PedPoolDiagError();
	});
#endif
});
#endif
