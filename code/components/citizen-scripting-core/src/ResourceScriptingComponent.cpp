/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "Resource.h"
#include "ResourceEventComponent.h"
#include "ResourceMetaDataComponent.h"
#include "ResourceScriptingComponent.h"

#ifndef IS_FXSERVER
#include <ICoreGameInit.h>
#endif

#include <Error.h>

namespace fx
{
static tbb::concurrent_queue<std::tuple<std::string, std::function<void()>>> g_onNetInitCbs;

OMPtr<IScriptHost> GetScriptHostForResource(Resource* resource);

ResourceScriptingComponent::ResourceScriptingComponent(Resource* resource)
	: m_resource(resource)
{
	resource->OnStart.Connect([=] ()
	{
		// preemptively instantiate all scripting environments
		std::vector<OMPtr<IScriptFileHandlingRuntime>> environments;

		{
			guid_t clsid;
			intptr_t findHandle = fxFindFirstImpl(IScriptFileHandlingRuntime::GetIID(), &clsid);

			if (findHandle != 0)
			{
				do
				{
					OMPtr<IScriptFileHandlingRuntime> ptr;

					if (FX_SUCCEEDED(MakeInterface(&ptr, clsid)))
					{
						environments.push_back(ptr);
					}
				} while (fxFindNextImpl(findHandle, &clsid));

				fxFindImplClose(findHandle);
			}
		}

		// get metadata and list scripting environments we *do* want to use
		{
			fwRefContainer<ResourceMetaDataComponent> metaData = resource->GetComponent<ResourceMetaDataComponent>();

			auto sharedScripts = metaData->GlobEntriesVector("shared_script");
			auto clientScripts = metaData->GlobEntriesVector(
#ifdef IS_FXSERVER
				"server_script"
#else
				"client_script"
#endif
			);

			for (auto it = environments.begin(); it != environments.end(); )
			{
				OMPtr<IScriptFileHandlingRuntime> ptr = *it;
				bool environmentUsed = false;

				for (auto& list : { sharedScripts, clientScripts })
				{
					for (auto& script : list)
					{
						if (ptr->HandlesFile(const_cast<char*>(script.c_str())))
						{
							environmentUsed = true;
							break;
						}
					}
				}

				if (!environmentUsed)
				{
					it = environments.erase(it);
				}
				else
				{
					it++;
				}
			}
		}

		// assign them to ourselves and create them
		for (auto& environment : environments)
		{
			OMPtr<IScriptRuntime> ptr;
			if (FX_SUCCEEDED(environment.As(&ptr)))
			{
				ptr->SetParentObject(resource);

				m_scriptRuntimes[ptr->GetInstanceId()] = ptr;
			}
		}

		if (!m_scriptRuntimes.empty() || m_resource->GetName() == "_cfx_internal")
		{
			m_scriptHost = GetScriptHostForResource(m_resource);

			auto loadScripts = [this]()
			{
				CreateEnvironments();

				m_resource->OnCreate();
			};

#ifdef IS_FXSERVER
			loadScripts();
#else
			if (Instance<ICoreGameInit>::Get()->HasVariable("networkInited"))
			{
				loadScripts();
			}
			else
			{
				auto ignoreFlag = std::make_shared<bool>(false);
				auto ignoreFlagWeak = std::weak_ptr{ ignoreFlag };

				g_onNetInitCbs.emplace("Starting " + m_resource->GetName(), [loadScripts, ignoreFlag]()
				{
					if (*ignoreFlag)
					{
						return;
					}

					loadScripts();
				});

				resource->OnStop.Connect([ignoreFlagWeak]()
				{
					auto ignoreFlag = ignoreFlagWeak.lock();

					if (ignoreFlag)
					{
						*ignoreFlag = true;
					}
				});
			}
#endif
		}
	});

	resource->OnTick.Connect([=] ()
	{
		for (const auto& [id, tickRuntime] : m_tickRuntimes)
		{
			tickRuntime->Tick();
		}
	});

	resource->OnStop.Connect([=] ()
	{
		if (m_resource->GetName() == "_cfx_internal")
		{
			return;
		}

		for (auto& environmentPair : m_scriptRuntimes)
		{
			environmentPair.second->Destroy();
		}

		m_tickRuntimes.clear();
		m_scriptRuntimes.clear();
	});
}

void ResourceScriptingComponent::CreateEnvironments()
{
	trace("Creating script environments for %s\n", m_resource->GetName());

	for (auto& environmentPair : m_scriptRuntimes)
	{
		auto hr = environmentPair.second->Create(m_scriptHost.GetRef());

		if (FX_FAILED(hr))
		{
			FatalError("Failed to create environment, hresult %x", hr);
		}
	}

	// initialize event handler calls
	fwRefContainer<ResourceEventComponent> eventComponent = m_resource->GetComponent<ResourceEventComponent>();

	assert(eventComponent.GetRef());

	if (eventComponent.GetRef())
	{
		// pre-cache event-handling runtimes
		std::vector<OMPtr<IScriptEventRuntime>> eventRuntimes;

		for (auto& environmentPair : m_scriptRuntimes)
		{
			OMPtr<IScriptEventRuntime> ptr;

			if (FX_SUCCEEDED(environmentPair.second.As(&ptr)))
			{
				eventRuntimes.push_back(ptr);
			}
		}

		// add the event
		eventComponent->OnTriggerEvent.Connect([=](const std::string& eventName, const std::string& eventPayload, const std::string& eventSource, bool* eventCanceled)
		{
			if (m_eventsHandled.find(eventName) == m_eventsHandled.end())
			{
				// '*' event is an override to accept all in this resource
				if (m_eventsHandled.find("*") == m_eventsHandled.end())
				{
					// skip the HLL ScRT
					return;
				}
			}

			// invoke the event runtime
			for (auto&& runtime : eventRuntimes)
			{
				result_t hr;

				if (FX_FAILED(hr = runtime->TriggerEvent(const_cast<char*>(eventName.c_str()), const_cast<char*>(eventPayload.c_str()), eventPayload.size(), const_cast<char*>(eventSource.c_str()))))
				{
					trace("Failed to execute event %s - %08x.\n", eventName.c_str(), hr);
				}
			}
		});
	}

	// pre-cache tick runtimes
	{
		for (auto& [ id, runtime ] : m_scriptRuntimes)
		{
			OMPtr<IScriptTickRuntime> tickRuntime;

			if (FX_SUCCEEDED(runtime.As(&tickRuntime)))
			{
				m_tickRuntimes[id] = tickRuntime;
			}
		}
	}

	// iterate over the runtimes and load scripts as requested
	for (auto& environmentPair : m_scriptRuntimes)
	{
		OMPtr<IScriptFileHandlingRuntime> ptr;

		fwRefContainer<ResourceMetaDataComponent> metaData = m_resource->GetComponent<ResourceMetaDataComponent>();

		auto sharedScripts = metaData->GlobEntriesVector("shared_script");
		auto clientScripts = metaData->GlobEntriesVector(
#ifdef IS_FXSERVER
			"server_script"
#else
			"client_script"
#endif
		);

		if (FX_SUCCEEDED(environmentPair.second.As(&ptr)))
		{
			for (auto& list : { sharedScripts, clientScripts }) {
				for (auto& script : list)
				{
					if (ptr->HandlesFile(const_cast<char*>(script.c_str())))
					{
						result_t hr = ptr->LoadFile(const_cast<char*>(script.c_str()));

						if (FX_FAILED(hr))
						{
							trace("Failed to load script %s.\n", script.c_str());
						}
					}
				}
			}
		}
	}
}
}

static InitFunction initFunction([]()
{
	fx::Resource::OnInitializeInstance.Connect([](fx::Resource* resource)
	{
		resource->SetComponent<fx::ResourceScriptingComponent>(new fx::ResourceScriptingComponent(resource));
	});
});

#ifndef IS_FXSERVER
DLL_EXPORT fwEvent<const std::string&> OnScriptInitStatus;

bool DLL_EXPORT UpdateScriptInitialization()
{
	using namespace std::chrono_literals;

	static bool wasExecuting;

	if (!fx::g_onNetInitCbs.empty())
	{
		std::chrono::high_resolution_clock::duration startTime = std::chrono::high_resolution_clock::now().time_since_epoch();

		std::tuple<std::string, std::function<void()>> initCallback;

		while (fx::g_onNetInitCbs.try_pop(initCallback))
		{
			OnScriptInitStatus(std::get<0>(initCallback));

			std::get<1>(initCallback)();

			wasExecuting = true;

			// break and yield after 2 seconds of script initialization so the game gets a chance to breathe
			if ((std::chrono::high_resolution_clock::now().time_since_epoch() - startTime) > 5ms)
			{
				trace("Still executing script initialization routines...\n");

				break;
			}
		}
	}

	if (fx::g_onNetInitCbs.empty() && wasExecuting)
	{
		OnScriptInitStatus("Awaiting scripts");

		wasExecuting = false;
	}

	return fx::g_onNetInitCbs.empty();
}
#endif
