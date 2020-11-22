#include <StdInc.h>

#include <ServerInstanceBase.h>
#include <ServerInstanceBaseRef.h>
#include <state/ServerGameState.h>

#include <ResourceManager.h>
#include <ScriptEngine.h>

#include <ScriptSerialization.h>
#include <MakePlayerEntityFunction.h>

namespace fx
{
void DisownEntityScript(const fx::sync::SyncEntityPtr& entity);
}

static InitFunction initFunction([]()
{
	auto makeEntityFunction = [](auto fn, uintptr_t defaultValue = 0)
	{
		return [=](fx::ScriptContext& context)
		{
			// get the current resource manager
			auto resourceManager = fx::ResourceManager::GetCurrent();

			// get the owning server instance
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

			// get the server's game state
			auto gameState = instance->GetComponent<fx::ServerGameState>();

			// parse the client ID
			auto id = context.GetArgument<uint32_t>(0);

			if (!id)
			{
				context.SetResult(defaultValue);
				return;
			}

			auto entity = gameState->GetEntity(id);

			if (!entity)
			{
				throw std::runtime_error(va("Tried to access invalid entity: %d", id));

				context.SetResult(defaultValue);
				return;
			}

			context.SetResult(fn(context, entity));
		};
	};

	struct scrVector
	{
		float x;
		int pad;
		float y;
		int pad2;
		float z;
		int pad3;
	};

	fx::ScriptEngine::RegisterNativeHandler("DOES_ENTITY_EXIST", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// parse the client ID
		auto id = context.GetArgument<uint32_t>(0);

		if (!id)
		{
			context.SetResult(false);
			return;
		}

		auto entity = gameState->GetEntity(id);

		if (!entity)
		{
			context.SetResult(false);
			return;
		}

		context.SetResult(true);
	});

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_ENTITY_FROM_NETWORK_ID", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// parse the client ID
		auto id = context.GetArgument<uint32_t>(0);

		if (!id)
		{
			context.SetResult(0);
			return;
		}

		auto entity = gameState->GetEntity(0, id);

		if (!entity)
		{
			context.SetResult(0);
			return;
		}

		context.SetResult(gameState->MakeScriptHandle(entity));
	});

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_ENTITY_OWNER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int retval = -1;
		auto entry = entity->GetClient();

		if (entry)
		{
			retval = entry->GetNetId();
		}

		return retval;
	}));

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_FIRST_ENTITY_OWNER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
    {
        int retval = -1;
        auto firstOwner = entity->GetFirstOwner();

        if (!entity->firstOwnerDropped)
        {
            retval = firstOwner->GetNetId();
        }

        return retval;
    }));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_COORDS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		float position[3];
		entity->syncTree->GetPosition(position);

		scrVector resultVec = { 0 };
		resultVec.x = position[0];
		resultVec.y = position[1];
		resultVec.z = position[2];

		return resultVec;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_VELOCITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		scrVector resultVec = { 0 };

		auto v = entity->syncTree->GetVelocity();

		if (v)
		{
			resultVec.x = v->velX;
			resultVec.y = v->velY;
			resultVec.z = v->velZ;
		}

		return resultVec;
	}));

	static const float pi = 3.14159265358979323846f;

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ROTATION_VELOCITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto av = entity->syncTree->GetAngVelocity();

		scrVector resultVec = { 0 };

		if (av)
		{
			resultVec.x = av->angVelX;
			resultVec.y = av->angVelY;
			resultVec.z = av->angVelZ;
		}

		return resultVec;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ROTATION", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		scrVector resultVec = { 0 };

		if (entity->type == fx::sync::NetObjEntityType::Player || entity->type == fx::sync::NetObjEntityType::Ped)
		{
			resultVec.x = 0.0f;
			resultVec.y = 0.0f;

			auto pn = entity->syncTree->GetPedOrientation();

			if (pn)
			{
				resultVec.z = pn->currentHeading * 180.0 / pi;
			}
		}
		else
		{
			auto en = entity->syncTree->GetEntityOrientation();
			auto on = entity->syncTree->GetObjectOrientation();

			if (en || on)
			{
				bool highRes = false;
				fx::sync::compressed_quaternion<11> quat;
				float rotX, rotY, rotZ;

				if (en)
				{
					quat = en->quat;
				}
				else if (on)
				{
					highRes = on->highRes;
					quat = on->quat;
					rotX = on->rotX;
					rotY = on->rotY;
					rotZ = on->rotZ;
				}

				if (highRes)
				{
					resultVec.x = rotX * 180.0 / pi;
					resultVec.y = rotY * 180.0 / pi;
					resultVec.z = rotZ * 180.0 / pi;
				}
				else
				{
					float qx, qy, qz, qw;
					quat.Save(qx, qy, qz, qw);

					auto m4 = glm::toMat4(glm::quat{ qw, qx, qy, qz });

					// common GTA rotation (2) is ZXY
					glm::extractEulerAngleZXY(m4, resultVec.z, resultVec.x, resultVec.y);

					resultVec.x = glm::degrees(resultVec.x);
					resultVec.y = glm::degrees(resultVec.y);
					resultVec.z = glm::degrees(resultVec.z);
				}
			}
		}

		return resultVec;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_HEADING", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		float heading = 0.0f;

		if (entity->type == fx::sync::NetObjEntityType::Player || entity->type == fx::sync::NetObjEntityType::Ped)
		{
			auto pn = entity->syncTree->GetPedOrientation();

			if (pn)
			{
				heading = pn->currentHeading * 180.0 / pi;
			}
		}
		else
		{
			auto en = entity->syncTree->GetEntityOrientation();

			if (en)
			{
#if 0
				heading = en->rotZ * 180.0 / pi;
#else
				float qx, qy, qz, qw;
				en->quat.Save(qx, qy, qz, qw);

				auto m4 = glm::toMat4(glm::quat{ qw, qx, qy, qz });

				float _, z;
				glm::extractEulerAngleZXY(m4, z, _, _);

				heading = glm::degrees(z);
#endif
			}
		}

		return (heading < 0) ? 360.0f + heading : heading;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_POPULATION_TYPE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		fx::sync::ePopType popType = fx::sync::POPTYPE_UNKNOWN;
		entity->syncTree->GetPopulationType(&popType);

		return popType;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_MODEL", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		uint32_t model = 0;
		entity->syncTree->GetModelHash(&model);

		return model;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_SCRIPT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		uint32_t script = 0;
		if (entity->syncTree->GetScriptHash(&script))
		{
			static std::string scriptName;
			scriptName.clear();

			auto resourceManager = fx::ResourceManager::GetCurrent();
			resourceManager->ForAllResources([script](const fwRefContainer<fx::Resource>& resource)
			{
				if (scriptName.empty())
				{
					std::string subName = resource->GetName();

					if (subName.length() > 63)
					{
						subName = subName.substr(0, 63);
					}

					if (HashString(subName.c_str()) == script)
					{
						scriptName = resource->GetName();
					}
				}
			});

			if (!scriptName.empty())
			{
				return scriptName.c_str();
			}
		}

		return (const char*)nullptr;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_TYPE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		switch (entity->type)
		{
		case fx::sync::NetObjEntityType::Automobile:
		case fx::sync::NetObjEntityType::Bike:
		case fx::sync::NetObjEntityType::Boat:
		case fx::sync::NetObjEntityType::Heli:
		case fx::sync::NetObjEntityType::Plane:
		case fx::sync::NetObjEntityType::Submarine:
		case fx::sync::NetObjEntityType::Trailer:
		case fx::sync::NetObjEntityType::Train:
		case fx::sync::NetObjEntityType::DraftVeh:
			return 2;
		case fx::sync::NetObjEntityType::Animal:
		case fx::sync::NetObjEntityType::Ped:
		case fx::sync::NetObjEntityType::Player:
		case fx::sync::NetObjEntityType::Horse:
			return 1;
		case fx::sync::NetObjEntityType::Object:
		case fx::sync::NetObjEntityType::Door:
		case fx::sync::NetObjEntityType::Pickup:
			return 3;
		default:
			return 0;
		}
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_ROUTING_BUCKET_POPULATION_ENABLED", [](fx::ScriptContext& context)
	{
		int bucket = context.GetArgument<int>(0);
		bool enabled = context.GetArgument<bool>(1);

		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		gameState->SetPopulationDisabled(bucket, !enabled);
	});

	fx::ScriptEngine::RegisterNativeHandler("SET_ROUTING_BUCKET_ENTITY_LOCKDOWN_MODE", [](fx::ScriptContext& context)
	{
		int bucket = context.GetArgument<int>(0);
		std::string_view sv = context.CheckArgument<const char*>(1);

		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		if (sv == "strict")
		{
			gameState->SetEntityLockdownMode(bucket, fx::EntityLockdownMode::Strict);
		}
		else if (sv == "relaxed")
		{
			gameState->SetEntityLockdownMode(bucket, fx::EntityLockdownMode::Relaxed);
		}
		else if (sv == "inactive")
		{
			gameState->SetEntityLockdownMode(bucket, fx::EntityLockdownMode::Inactive);
		}
	});

	fx::ScriptEngine::RegisterNativeHandler("SET_SYNC_ENTITY_LOCKDOWN_MODE", [](fx::ScriptContext& context)
	{
		std::string_view sv = context.CheckArgument<const char*>(0);

		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		if (sv == "strict")
		{
			gameState->SetEntityLockdownMode(fx::EntityLockdownMode::Strict);
		}
		else if (sv == "relaxed")
		{
			gameState->SetEntityLockdownMode(fx::EntityLockdownMode::Relaxed);
		}
		else if (sv == "inactive")
		{
			gameState->SetEntityLockdownMode(fx::EntityLockdownMode::Inactive);
		}
	});

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_NETWORK_ID_FROM_ENTITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->handle & 0xFFFF;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HASH_KEY", [](fx::ScriptContext& context)
	{
		context.SetResult(HashString(context.CheckArgument<const char*>(0)));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_ALL_PEDS", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (entity && (entity->type == fx::sync::NetObjEntityType::Animal ||
				entity->type == fx::sync::NetObjEntityType::Ped ||
				entity->type == fx::sync::NetObjEntityType::Player ||
				entity->type == fx::sync::NetObjEntityType::Horse))
			{
				entityList.push_back(gameState->MakeScriptHandle(entity));
			}
		}

		context.SetResult(fx::SerializeObject(entityList));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_ALL_VEHICLES", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (entity && (entity->type == fx::sync::NetObjEntityType::Automobile ||
				entity->type == fx::sync::NetObjEntityType::Bike ||
				entity->type == fx::sync::NetObjEntityType::Boat ||
				entity->type == fx::sync::NetObjEntityType::Heli ||
				entity->type == fx::sync::NetObjEntityType::Plane ||
				entity->type == fx::sync::NetObjEntityType::Submarine ||
				entity->type == fx::sync::NetObjEntityType::Trailer ||
				entity->type == fx::sync::NetObjEntityType::Train ||
				entity->type == fx::sync::NetObjEntityType::DraftVeh))
			{
				entityList.push_back(gameState->MakeScriptHandle(entity));
			}
		}

		context.SetResult(fx::SerializeObject(entityList));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_ALL_OBJECTS", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (entity && (entity->type == fx::sync::NetObjEntityType::Object ||
				entity->type == fx::sync::NetObjEntityType::Door))
			{
				entityList.push_back(gameState->MakeScriptHandle(entity));
			}
		}

		context.SetResult(fx::SerializeObject(entityList));
	});

	fx::ScriptEngine::RegisterNativeHandler("DELETE_ENTITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		gameState->DeleteEntity(entity);

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_AS_NO_LONGER_NEEDED", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// parse the client ID
		auto id = context.CheckArgument<uint32_t*>(0);

		if (!*id)
		{
			return;
		}

		auto entity = gameState->GetEntity(*id);

		if (!entity)
		{
			throw std::runtime_error(va("Tried to access invalid entity: %d", *id));
			return;
		}

		if (entity->GetClient())
		{
			// TODO: client-side set-as-no-longer-needed indicator
		}
		else
		{
			fx::DisownEntityScript(entity);
		}

		*id = 0;
	});

	fx::ScriptEngine::RegisterNativeHandler("ENSURE_ENTITY_STATE_BAG", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (!entity->stateBag)
		{
			// get the current resource manager
			auto resourceManager = fx::ResourceManager::GetCurrent();

			// get the owning server instance
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

			// get the server's game state
			auto gameState = instance->GetComponent<fx::ServerGameState>();
			entity->stateBag = gameState->GetStateBags()->RegisterStateBag(fmt::sprintf("entity:%d", entity->handle & 0xFFFF));

			std::set<int> rts{ -1 };

			for (auto i = entity->relevantTo.find_first(); i != decltype(entity->relevantTo)::kSize; i = entity->relevantTo.find_next(i))
			{
				rts.insert(i);
			}

			entity->stateBag->SetRoutingTargets(rts);

			auto client = entity->GetClient();

			if (client)
			{
				entity->stateBag->SetOwningPeer(client->GetSlotId());
			}
			else
			{
				entity->stateBag->SetOwningPeer(-1);
			}
		}

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_DISTANCE_CULLING_RADIUS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 1)
		{
			float radius = context.GetArgument<float>(1);
			entity->overrideCullingRadius = radius * radius;
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_ROUTING_BUCKET", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		auto [lock, clientData] = gameState->ExternalGetClientData(client);
		return int(clientData->routingBucket);
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_PLAYER_ROUTING_BUCKET", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		if (context.GetArgumentCount() > 1)
		{
			auto bucket = context.GetArgument<int>(1);

			if (bucket >= 0)
			{
				// get the current resource manager
				auto resourceManager = fx::ResourceManager::GetCurrent();

				// get the owning server instance
				auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

				// get the server's game state
				auto gameState = instance->GetComponent<fx::ServerGameState>();

				auto [lock, clientData] = gameState->ExternalGetClientData(client);
				gameState->ClearClientFromWorldGrid(client);
				clientData->routingBucket = bucket;

				fx::sync::SyncEntityPtr playerEntity;

				{
					std::shared_lock _lock(clientData->playerEntityMutex);
					playerEntity = clientData->playerEntity.lock();
				}

				if (playerEntity)
				{
					playerEntity->routingBucket = bucket;
				}
			}
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ROUTING_BUCKET", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return int(entity->routingBucket);
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_ROUTING_BUCKET", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 1)
		{
			auto bucket = context.GetArgument<int>(1);

			if (bucket >= 0)
			{
				entity->routingBucket = bucket;
			}
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_PLAYER_CULLING_RADIUS", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		if (context.GetArgumentCount() > 1)
		{
			float radius = context.GetArgument<float>(1);

			if (radius >= 0)
			{
				// get the current resource manager
				auto resourceManager = fx::ResourceManager::GetCurrent();

				// get the owning server instance
				auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

				// get the server's game state
				auto gameState = instance->GetComponent<fx::ServerGameState>();

				auto [lock, clientData] = gameState->ExternalGetClientData(client);
				clientData->playerCullingRadius = radius * radius;
			}
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_LANDING_GEAR_STATE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int gearState = 0;

		if (entity->type == fx::sync::NetObjEntityType::Heli)
		{
			auto state = entity->syncTree->GetHeliControl();
			if (state->hasLandingGear)
			{
				gearState = state->landingGearState;
			}
		}
		else if (entity->type == fx::sync::NetObjEntityType::Plane)
		{
			auto state = entity->syncTree->GetPlaneGameState();
			gearState = state->landingGearState;
		}

		return gearState;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_LOCK_ON_TARGET", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int lockOnHandle = 0;

		if (auto state = entity->syncTree->GetPlaneGameState())
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();

			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

			auto gameState = instance->GetComponent<fx::ServerGameState>();

			auto lockOnEntity = gameState->GetEntity(0, state->lockOnEntity);

			lockOnHandle = lockOnEntity ? gameState->MakeScriptHandle(lockOnEntity) : 0;
		}

		return lockOnHandle;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_HOMING_LOCKON_STATE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto state = entity->syncTree->GetPlaneGameState();

		return state ? state->lockOnState : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_FLIGHT_NOZZLE_POSITION", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto state = entity->syncTree->GetPlaneControl();

		return state ? state->nozzlePosition : 0.0f;
	}));
});
