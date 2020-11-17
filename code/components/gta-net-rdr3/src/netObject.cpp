#include <StdInc.h>

#include <Hooking.h>
#include <netObject.h>
#include <netSyncTree.h>

#include <atPool.h>

using TCreateCloneObjFn = rage::netObject* (*)(uint16_t objectId, uint8_t, int, int);
static TCreateCloneObjFn createCloneFuncs[(int)NetObjEntityType::Max];

// REDM1S: pool checks

#if 0
using TPoolPtr = atPoolBase**;
static TPoolPtr validatePools[(int)NetObjEntityType::Max];
#endif

namespace rage
{
netObject* CreateCloneObject(NetObjEntityType type, uint16_t objectId, uint8_t a2, int a3, int a4)
{
#if 0
	auto pool = *validatePools[(int)type];

	if (pool->GetCountDirect() >= pool->GetSize())
	{
		return nullptr;
	}
#endif

	return createCloneFuncs[(int)type](objectId, a2, a3, a4);
}
}

static HookFunction hookFunction([]()
{
	auto location = hook::get_pattern<char>("0F 8E ? 0F 00 00 41 8A 55", 20);

	createCloneFuncs[(int)NetObjEntityType::Object] = (TCreateCloneObjFn)hook::get_call(location);
	createCloneFuncs[(int)NetObjEntityType::Heli] = (TCreateCloneObjFn)hook::get_call(location + 0x6E);
	createCloneFuncs[(int)NetObjEntityType::Door] = (TCreateCloneObjFn)hook::get_call(location + 0xDD);
	createCloneFuncs[(int)NetObjEntityType::Boat] = (TCreateCloneObjFn)hook::get_call(location + 0x14B);
	createCloneFuncs[(int)NetObjEntityType::Bike] = (TCreateCloneObjFn)hook::get_call(location + 0x1B9);
	createCloneFuncs[(int)NetObjEntityType::Automobile] = (TCreateCloneObjFn)hook::get_call(location + 0x227);
	createCloneFuncs[(int)NetObjEntityType::Animal] = (TCreateCloneObjFn)hook::get_call(location + 0x29B);
	createCloneFuncs[(int)NetObjEntityType::Ped] = (TCreateCloneObjFn)hook::get_call(location + 0x30F);
	createCloneFuncs[(int)NetObjEntityType::Train] = (TCreateCloneObjFn)hook::get_call(location + 0x3B2);
	createCloneFuncs[(int)NetObjEntityType::Trailer] = (TCreateCloneObjFn)hook::get_call(location + 0x420);
	createCloneFuncs[(int)NetObjEntityType::Player] = (TCreateCloneObjFn)hook::get_call(location + 0x493);
	createCloneFuncs[(int)NetObjEntityType::Submarine] = (TCreateCloneObjFn)hook::get_call(location + 0x501);
	createCloneFuncs[(int)NetObjEntityType::Plane] = (TCreateCloneObjFn)hook::get_call(location + 0x56F);
	createCloneFuncs[(int)NetObjEntityType::PickupPlacement] = (TCreateCloneObjFn)hook::get_call(location + 0x5DE);
	createCloneFuncs[(int)NetObjEntityType::Pickup] = (TCreateCloneObjFn)hook::get_call(location + 0x64D);
	createCloneFuncs[(int)NetObjEntityType::DraftVeh] = (TCreateCloneObjFn)hook::get_call(location + 0x6BB);
	createCloneFuncs[(int)NetObjEntityType::WorldState] = (TCreateCloneObjFn)hook::get_call(location + 0x76E);
	createCloneFuncs[(int)NetObjEntityType::Horse] = (TCreateCloneObjFn)hook::get_call(location + 0x7E2);
	createCloneFuncs[(int)NetObjEntityType::Herd] = (TCreateCloneObjFn)hook::get_call(location + 0x850);
	createCloneFuncs[(int)NetObjEntityType::GroupScenario] = (TCreateCloneObjFn)hook::get_call(location + 0x8BF);
	createCloneFuncs[(int)NetObjEntityType::AnimScene] = (TCreateCloneObjFn)hook::get_call(location + 0x92D);
	createCloneFuncs[(int)NetObjEntityType::PropSet] = (TCreateCloneObjFn)hook::get_call(location + 0x99C);
	createCloneFuncs[(int)NetObjEntityType::StatsTracker] = (TCreateCloneObjFn)hook::get_call(location + 0xA0A);
	createCloneFuncs[(int)NetObjEntityType::WorldProjectile] = (TCreateCloneObjFn)hook::get_call(location + 0xA79);
	createCloneFuncs[(int)NetObjEntityType::Persistent] = (TCreateCloneObjFn)hook::get_call(location + 0xB1A);
	createCloneFuncs[(int)NetObjEntityType::PedSharedTargeting] = (TCreateCloneObjFn)hook::get_call(location + 0xB8E);
	createCloneFuncs[(int)NetObjEntityType::CombatDirector] = (TCreateCloneObjFn)hook::get_call(location + 0xC02);
	createCloneFuncs[(int)NetObjEntityType::PedGroup] = (TCreateCloneObjFn)hook::get_call(location + 0xC76);
	createCloneFuncs[(int)NetObjEntityType::Guardzone] = (TCreateCloneObjFn)hook::get_call(location + 0xCEA);
	createCloneFuncs[(int)NetObjEntityType::Incident] = (TCreateCloneObjFn)hook::get_call(location + 0xD56);
});
