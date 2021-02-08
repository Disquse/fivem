#pragma once

#include <state/ServerGameState.h>

#include <array>
#include <bitset>
#include <variant>

#include <boost/type_index.hpp>

namespace fx::sync
{
template<int Id1, int Id2, int Id3, bool CanSendOnFirst = true>
struct NodeIds
{
	inline static std::tuple<int, int, int> GetIds()
	{
		return { Id1, Id2, Id3 };
	}

	inline static bool CanSendOnFirstUpdate()
	{
		return CanSendOnFirst;
	}
};

inline bool shouldRead(SyncParseState& state, const std::tuple<int, int, int>& ids)
{
	if ((std::get<0>(ids) & state.syncType) == 0)
	{
		return false;
	}

	// because we hardcode this sync type to 0 (mA0), we can assume it's not used
	if (std::get<2>(ids) && !(state.objType & std::get<2>(ids)))
	{
		return false;
	}

	if ((std::get<1>(ids) & state.syncType) != 0)
	{
		if (!state.buffer.ReadBit())
		{
			return false;
		}
	}

	return true;
}

inline bool shouldWrite(SyncUnparseState& state, const std::tuple<int, int, int>& ids, bool defaultValue = true)
{
	if ((std::get<0>(ids) & state.syncType) == 0)
	{
		return false;
	}

	// because we hardcode this sync type to 0 (mA0), we can assume it's not used
	if (std::get<2>(ids) && !(state.objType & std::get<2>(ids)))
	{
		return false;
	}

	if ((std::get<1>(ids) & state.syncType) != 0)
	{
		state.buffer.WriteBit(defaultValue);

		return defaultValue;
	}

	return true;
}

// from https://stackoverflow.com/a/26902803
template<class F, class...Ts, std::size_t...Is>
void for_each_in_tuple(std::tuple<Ts...> & tuple, F func, std::index_sequence<Is...>) {
	using expander = int[];
	(void)expander {
		0, ((void)func(std::get<Is>(tuple)), 0)...
	};
}

template<class F, class...Ts>
void for_each_in_tuple(std::tuple<Ts...> & tuple, F func) {
	for_each_in_tuple(tuple, func, std::make_index_sequence<sizeof...(Ts)>());
}

template<class... TChildren>
struct ChildList
{

};

template<typename T, typename... TRest>
struct ChildList<T, TRest...>
{
	T first;
	ChildList<TRest...> rest;
};

template<typename T>
struct ChildList<T>
{
	T first;
};

template<typename T>
struct ChildListInfo
{

};

template<typename... TChildren>
struct ChildListInfo<ChildList<TChildren...>>
{
	static constexpr size_t Size = sizeof...(TChildren);
};

template<size_t I, typename T>
struct ChildListElement;

template<size_t I, typename T, typename... TChildren>
struct ChildListElement<I, ChildList<T, TChildren...>>
	: ChildListElement<I - 1, ChildList<TChildren...>>
{
};

template<typename T, typename... TChildren>
struct ChildListElement<0, ChildList<T, TChildren...>>
{
	using Type = T;
};

template<size_t I>
struct ChildListGetter
{
	template<typename... TChildren>
	static inline auto& Get(ChildList<TChildren...>& list)
	{
		return ChildListGetter<I - 1>::Get(list.rest);
	}

	template<typename TList>
	static inline constexpr size_t GetOffset(size_t offset = 0)
	{
		return ChildListGetter<I - 1>::template GetOffset<decltype(TList::rest)>(
			offset + offsetof(TList, rest)
		);
	}
};

template<>
struct ChildListGetter<0>
{
	template<typename... TChildren>
	static inline auto& Get(ChildList<TChildren...>& list)
	{
		return list.first;
	}

	template<typename TList>
	static inline constexpr size_t GetOffset(size_t offset = 0)
	{
		return offset +
			offsetof(TList, first);
	}
};

template<typename TTuple>
struct Foreacher
{
	template<typename TFn, size_t I = 0>
	static inline std::enable_if_t<(I == ChildListInfo<TTuple>::Size)> for_each_in_tuple(TTuple& tuple, const TFn& fn)
	{

	}

	template<typename TFn, size_t I = 0>
	static inline std::enable_if_t<(I != ChildListInfo<TTuple>::Size)> for_each_in_tuple(TTuple& tuple, const TFn& fn)
	{
		fn(ChildListGetter<I>::Get(tuple));

		for_each_in_tuple<TFn, I + 1>(tuple, fn);
	}
};

template<typename TIds, typename... TChildren>
struct ParentNode : public NodeBase
{
	ChildList<TChildren...> children;

	template<typename TData>
	inline static constexpr size_t GetOffsetOf()
	{
		return LoopChildren<TData>();
	}

	template<typename TData, size_t I = 0>
	inline static constexpr std::enable_if_t<I == sizeof...(TChildren), size_t> LoopChildren()
	{
		return 0;
	}

	template<typename TData, size_t I = 0>
	inline static constexpr std::enable_if_t<I != sizeof...(TChildren), size_t> LoopChildren()
	{
		size_t offset = ChildListElement<I, decltype(children)>::Type::template GetOffsetOf<TData>();

		if (offset != 0)
		{
			constexpr size_t elemOff = ChildListGetter<I>::template GetOffset<decltype(children)>();

			return offset + elemOff + offsetof(ParentNode, children);
		}

		return LoopChildren<TData, I + 1>();
	}

	template<typename TData>
	inline static constexpr size_t GetOffsetOfNode()
	{
		return LoopChildrenNode<TData>();
	}

	template<typename TData, size_t I = 0>
	inline static constexpr std::enable_if_t<I == sizeof...(TChildren), size_t> LoopChildrenNode()
	{
		return 0;
	}

	template<typename TData, size_t I = 0>
	inline static constexpr std::enable_if_t<I != sizeof...(TChildren), size_t> LoopChildrenNode()
	{
		size_t offset = ChildListElement<I, decltype(children)>::Type::template GetOffsetOfNode<TData>();

		if (offset != 0)
		{
			constexpr size_t elemOff = ChildListGetter<I>::template GetOffset<decltype(children)>();

			return offset + elemOff + offsetof(ParentNode, children);
		}

		return LoopChildrenNode<TData, I + 1>();
	}

	virtual bool Parse(SyncParseState& state) final override
	{
		if (shouldRead(state, TIds::GetIds()))
		{
			Foreacher<decltype(children)>::for_each_in_tuple(children, [&](auto& child)
			{
				child.Parse(state);
			});
		}

		return true;
	}

	virtual bool Unparse(SyncUnparseState& state) final override
	{
		bool should = false;

		// TODO: back out writes if we didn't write any child
		if (shouldWrite(state, TIds::GetIds()))
		{
			Foreacher<decltype(children)>::for_each_in_tuple(children, [&](auto& child)
			{
				bool thisShould = child.Unparse(state);

				should = should || thisShould;
			});
		}

		return should;
	}

	virtual bool Visit(const SyncTreeVisitor& visitor) final override
	{
		visitor(*this);

		Foreacher<decltype(children)>::for_each_in_tuple(children, [&](auto& child)
		{
			child.Visit(visitor);
		});

		return true;
	}

	virtual bool IsAdditional() override
	{
		return (std::get<2>(TIds::GetIds()) & 1);
	}
};

template<typename TIds, typename TNode, typename = void>
struct NodeWrapper : public NodeBase
{
	std::array<uint8_t, 1024> data;
	uint32_t length;

	TNode node;

	NodeWrapper()
		: length(0)
	{
		ackedPlayers.set();
	}

	template<typename TData>
	inline static constexpr size_t GetOffsetOf()
	{
		if constexpr (std::is_same_v<TNode, TData>)
		{
			return offsetof(NodeWrapper, node);
		}

		return 0;
	}

	template<typename TData>
	inline static constexpr size_t GetOffsetOfNode()
	{
		if constexpr (std::is_same_v<TNode, TData>)
		{
			return offsetof(NodeWrapper, ackedPlayers);
		}

		return 0;
	}

	virtual bool Parse(SyncParseState& state) final override
	{
		/*auto isWrite = state.buffer.ReadBit();

		if (!isWrite)
		{
			return true;
		}*/

		auto curBit = state.buffer.GetCurrentBit();

		if (shouldRead(state, TIds::GetIds()))
		{
			// read into data array
			auto length = state.buffer.Read<uint32_t>(13);
			auto endBit = state.buffer.GetCurrentBit();

			auto leftoverLength = length;

			auto oldData = data;

			this->length = leftoverLength;
			state.buffer.ReadBits(data.data(), std::min(uint32_t(data.size() * 8), leftoverLength));

			// hac
			timestamp = state.timestamp;

			state.buffer.SetCurrentBit(endBit);

			// parse
			node.Parse(state);

			//if (memcmp(oldData.data(), data.data(), data.size()) != 0)
			{
				//trace("resetting acks on node %s\n", boost::typeindex::type_id<TNode>().pretty_name());
				frameIndex = state.frameIndex;

				if (frameIndex > state.entity->lastFrameIndex)
				{
					state.entity->lastFrameIndex = frameIndex;
				}

				ackedPlayers.reset();
			}

			state.buffer.SetCurrentBit(endBit + length);
		}

		return true;
	}

	virtual bool Unparse(SyncUnparseState& state) final override
	{
		bool hasData = (length > 0);

		// do we even want to write?
		bool couldWrite = false;

		// we can only write if we have data
		if (hasData)
		{
			// if creating, ignore acks
			if (state.syncType == 1)
			{
				couldWrite = true;
			}
			// otherwise, we only want to write if the player hasn't acked
			else if (frameIndex > state.lastFrameIndex)
			{
				couldWrite = true;
			}
		}

		// enable this for boundary checks
		//state.buffer.Write(8, 0x5A);

		if (state.timestamp && state.timestamp != timestamp)
		{
			couldWrite = false;
		}

		if (state.isFirstUpdate)
		{
			if (!TIds::CanSendOnFirstUpdate())
			{
				couldWrite = false;
			}

			// if this doesn't need activation flags, don't write it
			if ((std::get<2>(TIds::GetIds()) & 1) == 0)
			{
				couldWrite = false;
			}
		}

		if (shouldWrite(state, TIds::GetIds(), couldWrite))
		{
			state.buffer.WriteBits(data.data(), length);

			return true;
		}

		return false;
	}

	virtual bool Visit(const SyncTreeVisitor& visitor) final override
	{
		visitor(*this);

		return true;
	}

	virtual bool IsAdditional() override
	{
		return (std::get<2>(TIds::GetIds()) & 1);
	}
};

struct ParseSerializer
{
	inline ParseSerializer(SyncParseState* state)
		: state(state)
	{
	}

	template<typename T>
	bool Serialize(int size, T& data)
	{
		return state->buffer.Read(size, &data);
	}

	bool Serialize(bool& data)
	{
		data = state->buffer.ReadBit();
		return true;
	}

	bool Serialize(int size, float div, float& data)
	{
		data = state->buffer.ReadFloat(size, div);
		return true;
	}

	bool SerializeSigned(int size, float div, float& data)
	{
		data = state->buffer.ReadSignedFloat(size, div);
		return true;
	}

	static constexpr bool isReader = true;
	SyncParseState* state;
};

struct UnparseSerializer
{
	inline UnparseSerializer(SyncUnparseState* state)
		: state(state)
	{
	}

	template<typename T>
	bool Serialize(int size, T& data)
	{
		state->buffer.Write<T>(size, data);
		return true;
	}

	bool Serialize(bool& data)
	{
		return state->buffer.WriteBit(data);
	}

	bool Serialize(int size, float div, float& data)
	{
		state->buffer.WriteFloat(size, div, data);
		return true;
	}

	bool SerializeSigned(int size, float div, float& data)
	{
		state->buffer.WriteSignedFloat(size, div, data);
		return true;
	}

	static constexpr bool isReader = false;
	SyncUnparseState* state;
};

template<typename TNode>
struct GenericSerializeDataNode
{
	bool Parse(SyncParseState& state)
	{
		auto self = static_cast<TNode*>(this);
		auto serializer = ParseSerializer{ &state };
		return self->Serialize(serializer);
	}

	bool Unparse(SyncUnparseState& state)
	{
		auto self = static_cast<TNode*>(this);
		auto serializer = UnparseSerializer{ &state };
		return self->Serialize(serializer);
	}
};

struct CVehicleCreationDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CAutomobileCreationDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CGlobalFlagsDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CDynamicEntityGameStateDataNode : GenericSerializeDataNode<CDynamicEntityGameStateDataNode>
{
	template<typename Serializer>
	bool Serialize(Serializer& s)
	{
		return true;
	}
};

struct CPhysicalGameStateDataNode : GenericSerializeDataNode<CPhysicalGameStateDataNode>
{
	bool isVisible;
	bool flag2;
	bool flag3;
	bool flag4;

	int val1;

	template<typename Serializer>
	bool Serialize(Serializer& s)
	{
		s.Serialize(isVisible);
		s.Serialize(flag2);
		s.Serialize(flag3);
		s.Serialize(flag4);

		if (flag4)
		{
			s.Serialize(3, val1);
		}
		else
		{
			val1 = 0;
		}

		return true;
	}
};

struct CVehicleGameStateDataNode
{
	CVehicleGameStateNodeData data;

	bool Parse(SyncParseState& state)
	{
		return true;
	}
};

struct CEntityScriptGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPhysicalScriptGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CVehicleScriptGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CEntityScriptInfoDataNode
{
	uint32_t m_scriptHash;
	uint32_t m_timestamp;

	bool Parse(SyncParseState& state)
	{
		auto hasScript = state.buffer.ReadBit();

		if (hasScript) // Has script info
		{
			// deserialize CGameScriptObjInfo

			// -> CGameScriptId

			// ---> rage::scriptId
			m_scriptHash = state.buffer.Read<uint32_t>(32);
			// ---> end

			m_timestamp = state.buffer.Read<uint32_t>(32);

			if (state.buffer.ReadBit())
			{
				auto positionHash = state.buffer.Read<uint32_t>(32);
			}

			if (state.buffer.ReadBit())
			{
				auto instanceId = state.buffer.Read<uint32_t>(7);
			}

			// -> end

			auto scriptObjectId = state.buffer.Read<uint32_t>(32);

			auto hostTokenLength = state.buffer.ReadBit() ? 16 : 3;
			auto hostToken = state.buffer.Read<uint32_t>(hostTokenLength);

			// end
		}
		else
		{
			m_scriptHash = 0;
		}

		return true;
	}

	bool Unparse(sync::SyncUnparseState& state)
	{
		rl::MessageBuffer& buffer = state.buffer;

		if (m_scriptHash)
		{
			buffer.WriteBit(true);

			buffer.Write<uint32_t>(32, m_scriptHash);
			buffer.Write<uint32_t>(32, m_timestamp);

			buffer.WriteBit(false);
			buffer.WriteBit(false);

			buffer.Write<uint32_t>(32, 12);

			buffer.WriteBit(false);
			buffer.Write<uint32_t>(3, 0);
		}
		else
		{
			buffer.WriteBit(false);
		}

		return true;
	}
};

struct CPhysicalAttachDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CVehicleAppearanceDataNode
{
	CVehicleAppearanceNodeData data;

	bool Parse(SyncParseState& state)
	{
		return true;
	}
};

struct CVehicleDamageStatusDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CVehicleComponentReservationDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CVehicleHealthDataNode
{
	CVehicleHealthNodeData data;

	bool Parse(SyncParseState& state)
	{
		return true;
	}
};

struct CVehicleTaskDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CSectorDataNode
{
	int m_sectorX;
	int m_sectorY;
	int m_sectorZ;

	bool Parse(SyncParseState& state)
	{
		auto sectorX = state.buffer.Read<int>(10);
		auto sectorY = state.buffer.Read<int>(10);
		auto sectorZ = state.buffer.Read<int>(6);

		m_sectorX = sectorX;
		m_sectorY = sectorY;
		m_sectorZ = sectorZ;

		state.entity->syncTree->CalculatePosition();

		return true;
	}

	bool Unparse(sync::SyncUnparseState& state)
	{
		rl::MessageBuffer& buffer = state.buffer;

		buffer.Write<int>(10, m_sectorX);
		buffer.Write<int>(10, m_sectorY);
		buffer.Write<int>(6, m_sectorZ);

		return true;
	}
};

struct CSectorPositionDataNode
{
	float m_posX;
	float m_posY;
	float m_posZ;

	bool Parse(SyncParseState& state)
	{
		auto posX = state.buffer.ReadFloat(12, 54.0f);
		auto posY = state.buffer.ReadFloat(12, 54.0f);
		auto posZ = state.buffer.ReadFloat(12, 69.0f);

		m_posX = posX;
		m_posY = posY;
		m_posZ = posZ;

		state.entity->syncTree->CalculatePosition();

		return true;
	}

	bool Unparse(sync::SyncUnparseState& state)
	{
		rl::MessageBuffer& buffer = state.buffer;
		buffer.WriteFloat(12, 54.0f, m_posX);
		buffer.WriteFloat(12, 54.0f, m_posY);
		buffer.WriteFloat(12, 69.0f, m_posZ);

		return true;
	}
};

struct CPedCreationDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CPedGameStateDataNode
{
	CPedGameStateNodeData data;

	bool Parse(SyncParseState& state)
	{
		return true;
	}
};

struct CEntityOrientationDataNode : GenericSerializeDataNode<CEntityOrientationDataNode>
{
	CEntityOrientationNodeData data;

	template<typename Serializer>
	bool Serialize(Serializer& s)
	{
#if 0
		auto rotX = state.buffer.ReadSigned<int>(9) * 0.015625f;
		auto rotY = state.buffer.ReadSigned<int>(9) * 0.015625f;
		auto rotZ = state.buffer.ReadSigned<int>(9) * 0.015625f;

		data.rotX = rotX;
		data.rotY = rotY;
		data.rotZ = rotZ;
#else
		s.Serialize(2, data.quat.largest);
		s.Serialize(11, data.quat.integer_a);
		s.Serialize(11, data.quat.integer_b);
		s.Serialize(11, data.quat.integer_c);
#endif

		return true;
	}
};

struct CPhysicalVelocityDataNode
{
	CPhysicalVelocityNodeData data;

	bool Parse(SyncParseState& state)
	{
		auto velX = state.buffer.ReadSigned<int>(12) * 0.0625f;
		auto velY = state.buffer.ReadSigned<int>(12) * 0.0625f;
		auto velZ = state.buffer.ReadSigned<int>(12) * 0.0625f;

		data.velX = velX;
		data.velY = velY;
		data.velZ = velZ;

		return true;
	}
};

struct CVehicleAngVelocityDataNode
{
	CVehicleAngVelocityNodeData data;

	bool Parse(SyncParseState& state)
	{
		auto hasNoVelocity = state.buffer.ReadBit();

		if (!hasNoVelocity)
		{
			auto velX = state.buffer.ReadSigned<int>(10) * 0.03125f;
			auto velY = state.buffer.ReadSigned<int>(10) * 0.03125f;
			auto velZ = state.buffer.ReadSigned<int>(10) * 0.03125f;

			data.angVelX = velX;
			data.angVelY = velY;
			data.angVelZ = velZ;
		}
		else
		{
			data.angVelX = 0.0f;
			data.angVelY = 0.0f;
			data.angVelZ = 0.0f;

			state.buffer.ReadBit();
		}

		return true;
	}
};

struct CVehicleSteeringDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CVehicleControlDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CVehicleGadgetDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CMigrationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPhysicalMigrationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPhysicalScriptMigrationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CVehicleProximityMigrationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CBikeGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CBoatGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CDoorCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CDoorMovementDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CDoorScriptInfoDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CDoorScriptGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CHeliHealthDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CHeliControlDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CObjectCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CObjectGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CObjectScriptGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPhysicalHealthDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CObjectSectorPosNode : GenericSerializeDataNode<CObjectSectorPosNode>
{
	bool highRes;
	float m_posX;
	float m_posY;
	float m_posZ;

	template<typename Serializer>
	bool Serialize(Serializer& s)
	{
		s.Serialize(highRes);

		int bits = (highRes) ? 20 : 12;

		s.Serialize(bits, 54.0f, m_posX);
		s.Serialize(bits, 54.0f, m_posY);
		s.Serialize(bits, 69.0f, m_posZ);

		if constexpr (Serializer::isReader)
		{
			s.state->entity->syncTree->CalculatePosition();
		}

		return true;
	}
};

struct CPhysicalAngVelocityDataNode
{
	CVehicleAngVelocityNodeData data;

	bool Parse(SyncParseState& state)
	{
		auto velX = state.buffer.ReadSigned<int>(10) * 0.03125f;
		auto velY = state.buffer.ReadSigned<int>(10) * 0.03125f;
		auto velZ = state.buffer.ReadSigned<int>(10) * 0.03125f;

		data.angVelX = velX;
		data.angVelY = velY;
		data.angVelZ = velZ;

		return true;
	}
};
//struct CPedCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedScriptCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
//struct CPedGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedComponentReservationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedScriptGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedAttachDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CPedHealthDataNode
{
	CPedHealthNodeData data;

	bool Parse(SyncParseState& state)
	{
		return true;
	}
};

struct CPedMovementGroupDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedAIDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedAppearanceDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CPedOrientationDataNode : GenericSerializeDataNode<CPedOrientationDataNode>
{
	CPedOrientationNodeData data;

	template<typename Serializer>
	bool Serialize(Serializer& s)
	{
		s.SerializeSigned(8, 6.28318548f, data.currentHeading);
		s.SerializeSigned(8, 6.28318548f, data.desiredHeading);

		return true;
	}
};

struct CPedMovementDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedTaskTreeDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedTaskSpecificDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CPedSectorPosMapNode
{
	float m_posX;
	float m_posY;
	float m_posZ;

	bool Parse(SyncParseState& state)
	{
		auto posX = state.buffer.ReadFloat(12, 54.0f);
		auto posY = state.buffer.ReadFloat(12, 54.0f);
		auto posZ = state.buffer.ReadFloat(12, 69.0f);

		m_posX = posX;
		m_posY = posY;
		m_posZ = posZ;

		state.entity->syncTree->CalculatePosition();

		// more data follows

		return true;
	}
};

struct CPedSectorPosNavMeshNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedInventoryDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedTaskSequenceDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPickupCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPickupScriptGameStateNode { bool Parse(SyncParseState& state) { return true; } };
struct CPickupSectorPosNode { bool Parse(SyncParseState& state) { return true; } };
struct CPickupPlacementCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPickupPlacementStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPlaneGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPlaneControlDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CSubmarineGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CSubmarineControlDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CTrainGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPlayerCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPlayerGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };

struct CPlayerAppearanceDataNode
{
	uint32_t model;

	bool Parse(SyncParseState& state)
	{
		model = state.buffer.Read<uint32_t>(32);

		return true;
	}
};

struct CPlayerPedGroupDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPlayerAmbientModelStreamingNode { bool Parse(SyncParseState& state) { return true; } };
struct CPlayerGamerDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPlayerExtendedGameStateNode { bool Parse(SyncParseState& state) { return true; } };

struct CPlayerSectorPosNode
{
	float m_posX;
	float m_posY;
	float m_posZ;

	uint16_t m_standingOnHandle;
	float m_standingOffsetX;
	float m_standingOffsetY;
	float m_standingOffsetZ;

	bool isStandingOn;

	bool Parse(SyncParseState& state)
	{
		auto posX = state.buffer.ReadFloat(12, 54.0f);
		auto posY = state.buffer.ReadFloat(12, 54.0f);
		auto posZ = state.buffer.ReadFloat(12, 69.0f);

		m_posX = posX;
		m_posY = posY;
		m_posZ = posZ;

		// extra data
		if (state.buffer.ReadBit())
		{
			// unknown fields
			state.buffer.ReadBit();
			state.buffer.ReadBit();

			// is standing on?
			bool isStandingOn = state.buffer.ReadBit();
			if (isStandingOn)
			{
				m_standingOnHandle = state.buffer.Read<int>(13); // Standing On
				m_standingOffsetX = state.buffer.ReadSignedFloat(14, 40.0f); // Standing On Local Offset X
				m_standingOffsetY = state.buffer.ReadSignedFloat(14, 40.0f); // Standing On Local Offset Y
				m_standingOffsetZ = state.buffer.ReadSignedFloat(10, 20.0f); // Standing On Local Offset Z
			}
			else
			{
				m_standingOnHandle = 0;
				m_standingOffsetX = 0.0f;
				m_standingOffsetY = 0.0f;
				m_standingOffsetZ = 0.0f;
			}

			isStandingOn = isStandingOn;
		}

		state.entity->syncTree->CalculatePosition();

		return true;
	}
};

struct CPlayerCameraDataNode
{
	CPlayerCameraNodeData data;

	bool Parse(SyncParseState& state)
	{
		bool freeCamOverride = state.buffer.ReadBit();

		if (freeCamOverride)
		{
			bool unk = state.buffer.ReadBit();

			float freeCamPosX = state.buffer.ReadSignedFloat(19, 27648.0f);
			float freeCamPosY = state.buffer.ReadSignedFloat(19, 27648.0f);
			float freeCamPosZ = state.buffer.ReadFloat(19, 4416.0f) - 1700.0f;

			// 2pi
			float cameraX = state.buffer.ReadSignedFloat(10, 6.2831855f);
			float cameraZ = state.buffer.ReadSignedFloat(10, 6.2831855f);

			data.camMode = 1;
			data.freeCamPosX = freeCamPosX;
			data.freeCamPosY = freeCamPosY;
			data.freeCamPosZ = freeCamPosZ;

			data.cameraX = cameraX;
			data.cameraZ = cameraZ;
		}
		else
		{
			bool hasPositionOffset = state.buffer.ReadBit();
			state.buffer.ReadBit();

			if (hasPositionOffset)
			{
				float camPosX = state.buffer.ReadSignedFloat(19, 16000.0f);
				float camPosY = state.buffer.ReadSignedFloat(19, 16000.0f);
				float camPosZ = state.buffer.ReadSignedFloat(19, 16000.0f);

				data.camMode = 2;

				data.camOffX = camPosX;
				data.camOffY = camPosY;
				data.camOffZ = camPosZ;
			}
			else
			{
				data.camMode = 0;
			}

			float cameraX = state.buffer.ReadSignedFloat(10, 6.2831855f);
			float cameraZ = state.buffer.ReadSignedFloat(10, 6.2831855f);

			data.cameraX = cameraX;
			data.cameraZ = cameraZ;

			// TODO
		}

		// TODO

		return true;
	}
};

struct CWorldProjectileDataNode
{
	int m_sectorX;
	int m_sectorY;
	int m_sectorZ;

	float m_posX;
	float m_posY;
	float m_posZ;

	bool Parse(SyncParseState& state)
	{
		bool isAttached = state.buffer.ReadBit();

		if (!isAttached)
		{
			m_sectorX = state.buffer.Read<int>(10);
			m_sectorY = state.buffer.Read<int>(10);
			m_sectorZ = state.buffer.Read<int>(6);

			m_posX = state.buffer.ReadFloat(20, 54.0f);
			m_posY = state.buffer.ReadFloat(20, 54.0f);
			m_posZ = state.buffer.ReadFloat(20, 69.0f);
		}
		else
		{
			bool unk = state.buffer.ReadBit();

			m_sectorX = 512;
			m_sectorY = 512;
			m_sectorZ = 0;

			m_posX = state.buffer.ReadFloat(16, 17.0f);
			m_posY = state.buffer.ReadFloat(16, 17.0f);
			m_posZ = state.buffer.ReadFloat(10, 8.0f);
		}

		// TODO

		return true;
	}
};

struct CHerdPositionNode
{
	float m_posX;
	float m_posY;
	float m_posZ;

	bool Parse(SyncParseState& state)
	{
		auto unk = state.buffer.Read<int>(19);
		bool unk2 = state.buffer.ReadBit();

		if (unk2)
		{
			auto unk3 = state.buffer.Read<uint8_t>(3);

			m_posX = state.buffer.ReadFloat(6, 5.0f);
			m_posY = state.buffer.ReadFloat(10, 300.0f);
			m_posZ = state.buffer.ReadFloat(6, 20.0f);
		}
		else
		{
			m_posX = 0.0f;
			m_posY = 0.0f;
			m_posZ = 0.0f;
		}

		return true;
	}
};

struct CPlayerWantedAndLOSDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CDraftVehCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CStatsTrackerGameStateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CWorldStateBaseDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CIncidentCreateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CGuardzoneCreateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedGroupCreateDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CAnimalCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CProjectileCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPedStandingOnObjectDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CProjectileAttachNode { bool Parse(SyncParseState& state) { return true; } };
struct CHerdMemberDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CHerdGameDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CAnimSceneCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CAnimSceneFrequentDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CAnimSceneInfrequentDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CGroupScenarioFrequentDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CGroupScenarioEntitiesDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CGroupScenarioCreationDataNode { bool Parse(SyncParseState& state) { return true; } };
struct CPropSetCreationDataNode { bool Parse(SyncParseState& state) { return true; } };

// REDM1S: unknown rdr3 nodes (addresses are 1311.20)
struct DataNode_1435984c0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143598330 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143598fb0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143598e20 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143598b00 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143594ab8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359b8a8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143599140 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435992d0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359e920 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359e790 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143599dc0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435995f0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143599780 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143599910 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143599aa0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143599c30 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143599f50 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359a8b0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359aa40 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143598c90 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359eab0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359ec40 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359a590 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359abd0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359ad88 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435987e0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359a270 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143596d38 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143596ed0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143597068 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143597390 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143596880 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143597200 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143595f10 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435960a8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359f5a0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143594478 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143594dd8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435950f8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143595418 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359a400 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359a720 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359b588 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359b3f8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359b718 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359ba38 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359bbc8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359b0d8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a0a20 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359cd00 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359ce90 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359d020 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359d4d0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359d340 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359d7f0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359d980 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359db10 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359cb70 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359e2e0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359d1b0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359dca0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359de30 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359dfc0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359e150 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359c9e0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359d660 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a03d0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a0238 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a0568 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a1838 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a19c8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a1b58 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a0ed8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a1068 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435931b8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143593348 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435934d8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143593668 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435937f8 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143595740 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435958d0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143595a60 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143595bf0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435929e0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143592b70 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143592e90 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_143592d00 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359bd58 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359bef0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359c080 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359c210 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_14359c3a0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a1e78 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a2010 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a21a0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a2330 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a24c0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a2658 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435981a0 { bool Parse(SyncParseState& state) { return true; } };
struct DataNode_1435a1ce8 { bool Parse(SyncParseState& state) { return true; } };

template<typename TNode>
struct SyncTree : public SyncTreeBase
{
	TNode root;
	std::mutex mutex;

	template<typename TData>
	inline static constexpr size_t GetOffsetOf()
	{
		auto doff = TNode::template GetOffsetOf<TData>();

		return (doff) ? offsetof(SyncTree, root) + doff : 0;
	}

	template<typename TData>
	inline std::tuple<bool, TData*> GetData()
	{
		constexpr auto offset = GetOffsetOf<TData>();

		if constexpr (offset != 0)
		{
			return { true, (TData*)((uintptr_t)this + offset) };
		}

		return { false, nullptr };
	}

	template<typename TData>
	inline static constexpr size_t GetOffsetOfNode()
	{
		auto doff = TNode::template GetOffsetOfNode<TData>();

		return (doff) ? offsetof(SyncTree, root) + doff : 0;
	}

	template<typename TData>
	inline NodeWrapper<NodeIds<0, 0, 0>, TData>* GetNode()
	{
		constexpr auto offset = GetOffsetOfNode<TData>();

		if constexpr (offset != 0)
		{
			return (NodeWrapper<NodeIds<0, 0, 0>, TData>*)((uintptr_t)this + offset - 8);
		}

		return nullptr;
	}

	virtual void GetPosition(float* posOut) override
	{
		auto [hasSdn, secDataNode] = GetData<CSectorDataNode>();
		auto [hasSpdn, secPosDataNode] = GetData<CSectorPositionDataNode>();
		auto [hasWpdn, projectileDataNode] = GetData<CWorldProjectileDataNode>();
		auto [hasPspdn, playerSecPosDataNode] = GetData<CPlayerSectorPosNode>();
		auto [hasOspdn, objectSecPosDataNode] = GetData<CObjectSectorPosNode>();
		auto [hasPspmdn, pedSecPosMapDataNode] = GetData<CPedSectorPosMapNode>();
		auto [hasDoor, doorCreationDataNode] = GetData<CDoorCreationDataNode>();
		auto [hasPgsdn, pedGameStateDataNode] = GetData<CPedGameStateDataNode>();
		auto [hasHpn, herdPosNode] = GetData<CHerdPositionNode>();

		auto sectorX = (hasSdn) ? secDataNode->m_sectorX : (hasWpdn) ? projectileDataNode->m_sectorX : 512;
		auto sectorY = (hasSdn) ? secDataNode->m_sectorY : (hasWpdn) ? projectileDataNode->m_sectorY : 512;
		auto sectorZ = (hasSdn) ? secDataNode->m_sectorZ : (hasWpdn) ? projectileDataNode->m_sectorZ : 0;

		auto sectorPosX =
			(hasSpdn) ? secPosDataNode->m_posX :
				(hasPspdn) ? playerSecPosDataNode->m_posX :
					(hasOspdn) ? objectSecPosDataNode->m_posX :
						(hasPspmdn) ? pedSecPosMapDataNode->m_posX :
							(hasWpdn) ? projectileDataNode->m_posX :
								(hasHpn) ? herdPosNode->m_posX :
									0.0f;

		auto sectorPosY =
			(hasSpdn) ? secPosDataNode->m_posY :
				(hasPspdn) ? playerSecPosDataNode->m_posY :
					(hasOspdn) ? objectSecPosDataNode->m_posY :
						(hasPspmdn) ? pedSecPosMapDataNode->m_posY :
							(hasWpdn) ? projectileDataNode->m_posY :
								(hasHpn) ? herdPosNode->m_posY :
									0.0f;

		auto sectorPosZ =
			(hasSpdn) ? secPosDataNode->m_posZ :
				(hasPspdn) ? playerSecPosDataNode->m_posZ :
					(hasOspdn) ? objectSecPosDataNode->m_posZ :
						(hasPspmdn) ? pedSecPosMapDataNode->m_posZ :
							(hasWpdn) ? projectileDataNode->m_posZ :
								(hasHpn) ? herdPosNode->m_posZ :
									0.0f;

		if (sectorPosX == 0.0f && sectorPosY == 0.0f && sectorPosZ == 0.0f)
		{
			trace("warning | %d %d %d\n", sectorX, sectorY, sectorZ);
		}

		posOut[0] = ((sectorX - 512.0f) * 54.0f) + sectorPosX;
		posOut[1] = ((sectorY - 512.0f) * 54.0f) + sectorPosY;
		posOut[2] = ((sectorZ * 69.0f) + sectorPosZ) - 1700.0f;

		/*
		if (hasDoor)
		{
			posOut[0] = doorCreationDataNode->m_posX;
			posOut[1] = doorCreationDataNode->m_posY;
			posOut[2] = doorCreationDataNode->m_posZ;
		}
		*/

		if (hasPspdn)
		{
			// trace("sector pos %d %d %d | location: %f %f %f\n", sectorX, sectorY, sectorZ, posOut[0], posOut[1], posOut[2]);

			/*
			if (g_serverGameState && playerSecPosDataNode->isStandingOn)
			{
				auto entity = g_serverGameState->GetEntity(0, playerSecPosDataNode->m_standingOnHandle);

				if (entity && entity->type != sync::NetObjEntityType::Player)
				{
					entity->syncTree->GetPosition(posOut);

					posOut[0] += playerSecPosDataNode->m_standingOffsetX;
					posOut[1] += playerSecPosDataNode->m_standingOffsetY;
					posOut[2] += playerSecPosDataNode->m_standingOffsetZ;
				}
			}
			*/
		}

		/*
		// if in a vehicle, force the current vehicle's position to be used
		if (hasPgsdn)
		{
			if (g_serverGameState && pedGameStateDataNode->data.curVehicle != -1)
			{
				auto entity = g_serverGameState->GetEntity(0, pedGameStateDataNode->data.curVehicle);

				if (entity && entity->type != fx::sync::NetObjEntityType::Ped && entity->type != fx::sync::NetObjEntityType::Player)
				{
					entity->syncTree->GetPosition(posOut);
				}
			}
		}
		*/
	}

	virtual CPlayerCameraNodeData* GetPlayerCamera() override
	{
		auto [hasCdn, cameraNode] = GetData<CPlayerCameraDataNode>();

		return (hasCdn) ? &cameraNode->data : nullptr;
	}

	virtual CPedGameStateNodeData* GetPedGameState() override
	{
		auto [hasPdn, pedNode] = GetData<CPedGameStateDataNode>();

		return (hasPdn) ? &pedNode->data : nullptr;
	}

	virtual CVehicleGameStateNodeData* GetVehicleGameState() override
	{
		auto [hasVdn, vehNode] = GetData<CVehicleGameStateDataNode>();

		return (hasVdn) ? &vehNode->data : nullptr;
	}

	virtual CPedOrientationNodeData* GetPedOrientation() override
	{
		auto [hasNode, node] = GetData<CPedOrientationDataNode>();

		return (hasNode) ? &node->data : nullptr;
	}

	virtual CEntityOrientationNodeData* GetEntityOrientation() override
	{
		auto [hasNode, node] = GetData<CEntityOrientationDataNode>();

		return (hasNode) ? &node->data : nullptr;
	}

	virtual CObjectOrientationNodeData* GetObjectOrientation() override
	{
		auto [hasNode, node] = GetData<CObjectOrientationDataNode>();

		return (hasNode) ? &node->data : nullptr;
	}

	virtual CVehicleAngVelocityNodeData* GetAngVelocity() override
	{
		{
			auto [hasNode, node] = GetData<CVehicleAngVelocityDataNode>();

			if (hasNode)
			{
				return &node->data;
			}
		}

		auto [hasNode, node] = GetData<CPhysicalAngVelocityDataNode>();

		return (hasNode) ? &node->data : nullptr;
	}

	virtual CPhysicalVelocityNodeData* GetVelocity() override
	{
		auto [hasNode, node] = GetData<CPhysicalVelocityDataNode>();

		return (hasNode) ? &node->data : nullptr;
	}

	virtual void CalculatePosition() override
	{
		// TODO: cache it?
	}

	virtual bool GetPopulationType(ePopType* popType) override
	{
#if 0
		auto[hasVcn, vehCreationNode] = GetData<CVehicleCreationDataNode>();

		if (hasVcn)
		{
			*popType = vehCreationNode->m_popType;
			return true;
		}

		auto[hasPcn, pedCreationNode] = GetData<CPedCreationDataNode>();

		if (hasPcn)
		{
			*popType = pedCreationNode->m_popType;
			return true;
		}

		// TODO: objects(?)
#endif

		return false;
	}

	virtual bool GetModelHash(uint32_t* modelHash) override
	{
#if 0
		auto[hasVcn, vehCreationNode] = GetData<CVehicleCreationDataNode>();

		if (hasVcn)
		{
			*modelHash = vehCreationNode->m_model;
			return true;
		}

		auto[hasPan, playerAppearanceNode] = GetData<CPlayerAppearanceDataNode>();

		if (hasPan)
		{
			*modelHash = playerAppearanceNode->model;
			return true;
		}

		auto[hasPcn, pedCreationNode] = GetData<CPedCreationDataNode>();

		if (hasPcn)
		{
			*modelHash = pedCreationNode->m_model;
			return true;
		}

		auto[hasOcn, objectCreationNode] = GetData<CObjectCreationDataNode>();

		if (hasOcn)
		{
			*modelHash = objectCreationNode->m_model;
			return true;
		}
#endif

		return false;
	}

	virtual bool GetScriptHash(uint32_t* scriptHash) override
	{
		auto[hasSin, scriptInfoNode] = GetData<CEntityScriptInfoDataNode>();

		if (hasSin)
		{
			*scriptHash = scriptInfoNode->m_scriptHash;
			return true;
		}

		return false;
	}

	virtual void Parse(SyncParseState& state) final override
	{
		std::unique_lock<std::mutex> lock(mutex);

		//trace("parsing root\n");
		state.objType = 0;

		if (state.syncType == 2 || state.syncType == 4)
		{
			// mA0 flag
			state.objType = state.buffer.ReadBit();
		}

		// REDM1S: only RDR3
		state.buffer.ReadBit();

		root.Parse(state);
	}

	virtual bool Unparse(SyncUnparseState& state) final override
	{
		std::unique_lock<std::mutex> lock(mutex);

		state.objType = 0;

		if (state.syncType == 2 || state.syncType == 4)
		{
			state.objType = 1;

			state.buffer.WriteBit(1);
		}

		// REDM1S: only RDR3
		state.buffer.WriteBit(0);

		return root.Unparse(state);
	}

	virtual void Visit(const SyncTreeVisitor& visitor) final override
	{
		std::unique_lock<std::mutex> lock(mutex);

		root.Visit(visitor);
	}
};

using CAnimalSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CAnimalCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435984c0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598330>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435981a0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598fb0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598e20>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598b00>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359b8a8>
				>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, DataNode_143599140>,
					NodeWrapper<NodeIds<127, 127, 1>, DataNode_1435992d0>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359e920>,
					NodeWrapper<NodeIds<127, 127, 1>, DataNode_14359e790>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 1>, DataNode_143599dc0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435995f0>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_143599780>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_143599910>
			>,
			ParentNode<
				NodeIds<87, 87, 0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599aa0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599c30>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599f50>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359a8b0>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_14359aa40>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143598c90>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359eab0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ec40>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359a590>
		>,
		ParentNode<
			NodeIds<87, 87, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedStandingOnObjectDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedMovementDataNode>,
			ParentNode<
				NodeIds<87, 87, 0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedSectorPosMapNode>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_1435987e0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359a270>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 4, 1>, CPedTaskSequenceDataNode>
		>
	>
>;
using CAutomobileSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>,
			NodeWrapper<NodeIds<1, 0, 0>, CAutomobileCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 87, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>
			>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CBikeSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 87, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CBikeGameStateDataNode>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>
			>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CBoatSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 87, 0>,
			ParentNode<
				NodeIds<127, 87, 0>,
				ParentNode<
					NodeIds<127, 87, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>,
					NodeWrapper<NodeIds<87, 87, 0>, CBoatGameStateDataNode>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>
			>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CDoorSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CDoorCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 127, 0>,
			NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595f10>
		>,
		ParentNode<
			NodeIds<127, 127, 0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435960a8>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>
		>
	>
>;
using CHeliSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>,
			NodeWrapper<NodeIds<1, 0, 0>, CAutomobileCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_14359f5a0>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>,
				NodeWrapper<NodeIds<86, 86, 0>, CHeliControlDataNode>
			>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CObjectSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CObjectCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 127, 0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594478>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594dd8>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435950f8>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435950f8>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CObjectScriptGameStateDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>
		>,
		ParentNode<
			NodeIds<87, 87, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143595418>,
			NodeWrapper<NodeIds<87, 87, 0>, CObjectSectorPosNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalAngVelocityDataNode>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>
		>
	>
>;
using CPedSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CPedCreationDataNode>,
			NodeWrapper<NodeIds<1, 0, 1>, CPedScriptCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435984c0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598330>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435981a0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598fb0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598e20>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598b00>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359b8a8>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, DataNode_143599140>,
					NodeWrapper<NodeIds<127, 127, 1>, DataNode_1435992d0>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 1>, DataNode_143599dc0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435995f0>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_143599780>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_143599910>
			>,
			ParentNode<
				NodeIds<87, 87, 0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599aa0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599c30>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599f50>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359a8b0>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_14359aa40>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143598c90>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359a400>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359a720>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359b588>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359b3f8>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359b718>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ba38>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359bbc8>,
			NodeWrapper<NodeIds<87, 87, 1>, DataNode_14359b0d8>
		>,
		ParentNode<
			NodeIds<87, 87, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedStandingOnObjectDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedMovementDataNode>,
			ParentNode<
				NodeIds<87, 87, 0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedSectorPosMapNode>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_1435987e0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359a270>
		>,
		ParentNode<
			NodeIds<5, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<5, 0, 0>, CPedInventoryDataNode>,
			NodeWrapper<NodeIds<4, 4, 1>, CPedTaskSequenceDataNode>
		>
	>
>;
using CPickupSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CPickupCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 127, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>
			>,
			ParentNode<
				NodeIds<127, 127, 1>,
				NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>,
				NodeWrapper<NodeIds<127, 127, 1>, CPickupScriptGameStateNode>,
				NodeWrapper<NodeIds<127, 127, 1>, CPhysicalGameStateDataNode>,
				NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
				NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
				NodeWrapper<NodeIds<127, 127, 1>, CPhysicalHealthDataNode>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>
		>,
		ParentNode<
			NodeIds<87, 87, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPickupSectorPosNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalAngVelocityDataNode>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>
		>
	>
>;
using CPickupPlacementSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<1, 0, 0>, CPickupPlacementCreationDataNode>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CPickupPlacementStateDataNode>
	>
>;
using CPlaneSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 87, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a0a20>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>,
				NodeWrapper<NodeIds<86, 86, 0>, CPlaneControlDataNode>
			>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CSubmarineSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 87, 0>,
			ParentNode<
				NodeIds<127, 87, 0>,
				ParentNode<
					NodeIds<127, 87, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>,
					NodeWrapper<NodeIds<87, 87, 0>, CSubmarineGameStateDataNode>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>,
				NodeWrapper<NodeIds<86, 86, 0>, CSubmarineControlDataNode>
			>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CPlayerSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CPlayerCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			ParentNode<
				NodeIds<127, 87, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435984c0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598330>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435981a0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598fb0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598e20>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598b00>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359b8a8>
				>,
				ParentNode<
					NodeIds<127, 87, 0>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359cd00>,
					NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ce90>,
					NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359d020>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 1>, DataNode_143599dc0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435995f0>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_143599780>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_143599910>
			>,
			ParentNode<
				NodeIds<87, 87, 0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599aa0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599c30>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599f50>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359a8b0>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_14359aa40>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143598c90>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359d4d0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359d340>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_14359d7f0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359d980>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359db10>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359cb70>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359e2e0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359b3f8>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359b718>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359d1b0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359dca0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359de30>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359dfc0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359dfc0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359dfc0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359dfc0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359dfc0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359e150>,
			NodeWrapper<NodeIds<87, 87, 1>, DataNode_14359b0d8>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359a720>
		>,
		ParentNode<
			NodeIds<87, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedStandingOnObjectDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedMovementDataNode>,
			ParentNode<
				NodeIds<87, 87, 0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPlayerSectorPosNode>,
			NodeWrapper<NodeIds<86, 86, 0>, CPlayerCameraDataNode>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_14359c9e0>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_14359d660>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>
		>
	>
>;
using CTrailerSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>,
			NodeWrapper<NodeIds<1, 0, 0>, CAutomobileCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 87, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>
			>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CTrainSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 87, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a03d0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a0238>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a0568>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>
			>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CDraftVehSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CVehicleCreationDataNode>,
			NodeWrapper<NodeIds<1, 0, 0>, CAutomobileCreationDataNode>,
			NodeWrapper<NodeIds<1, 0, 0>, CDraftVehCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CVehicleGameStateDataNode>
				>,
				ParentNode<
					NodeIds<127, 127, 1>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CVehicleScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 0>, CPhysicalAttachDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596d38>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596ed0>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597068>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143597390>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_143596880>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143597200>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a1838>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a19c8>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_1435a1b58>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CVehicleAngVelocityDataNode>,
			ParentNode<
				NodeIds<127, 86, 0>,
				NodeWrapper<NodeIds<86, 86, 0>, CVehicleSteeringDataNode>,
				NodeWrapper<NodeIds<87, 87, 0>, CVehicleControlDataNode>,
				NodeWrapper<NodeIds<127, 127, 0>, CVehicleGadgetDataNode>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_1435a1ce8>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CVehicleProximityMigrationDataNode>
		>
	>
>;
using CStatsTrackerSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 127, 0>,
		NodeWrapper<NodeIds<127, 127, 0>, CStatsTrackerGameStateDataNode>
	>
>;
using CPropSetSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<1, 0, 0>, CPropSetCreationDataNode>,
		NodeWrapper<NodeIds<87, 87, 0>, CEntityScriptInfoDataNode>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a0ed8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a1068>
	>
>;
using CAnimSceneSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<87, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CAnimSceneCreationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityScriptInfoDataNode>
		>,
		ParentNode<
			NodeIds<127, 127, 0>,
			NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, CAnimSceneInfrequentDataNode>
		>,
		ParentNode<
			NodeIds<86, 86, 0>,
			NodeWrapper<NodeIds<86, 86, 0>, CAnimSceneFrequentDataNode>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>
		>
	>
>;
using CGroupScenarioSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<87, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CGroupScenarioCreationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CEntityScriptInfoDataNode>
		>,
		ParentNode<
			NodeIds<127, 127, 0>,
			NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
			NodeWrapper<NodeIds<127, 127, 0>, CGroupScenarioEntitiesDataNode>
		>,
		ParentNode<
			NodeIds<86, 86, 0>,
			NodeWrapper<NodeIds<86, 86, 0>, CGroupScenarioFrequentDataNode>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>
		>
	>
>;
using CHerdSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
		NodeWrapper<NodeIds<87, 87, 0>, CEntityScriptInfoDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
		NodeWrapper<NodeIds<87, 87, 0>, CHerdGameDataNode>,
		NodeWrapper<NodeIds<87, 87, 0>, CHerdMemberDataNode>,
		NodeWrapper<NodeIds<87, 87, 0>, CHerdPositionNode>
	>
>;
using CHorseSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CAnimalCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 86, 0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CDynamicEntityGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, CPhysicalGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435984c0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598330>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435981a0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598fb0>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598e20>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143598b00>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_143594ab8>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359b8a8>
				>,
				ParentNode<
					NodeIds<127, 127, 0>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, CPhysicalScriptGameStateDataNode>,
					NodeWrapper<NodeIds<127, 127, 1>, DataNode_143599140>,
					NodeWrapper<NodeIds<127, 127, 1>, DataNode_1435992d0>,
					NodeWrapper<NodeIds<127, 127, 1>, CEntityScriptInfoDataNode>,
					NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359e920>,
					NodeWrapper<NodeIds<127, 127, 1>, DataNode_14359e790>
				>
			>,
			NodeWrapper<NodeIds<127, 127, 1>, DataNode_143599dc0>,
			ParentNode<
				NodeIds<127, 127, 0>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435995f0>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_143599780>,
				NodeWrapper<NodeIds<127, 127, 0>, DataNode_143599910>
			>,
			ParentNode<
				NodeIds<87, 87, 0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599aa0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599c30>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143599f50>,
			NodeWrapper<NodeIds<127, 127, 0>, DataNode_14359a8b0>,
			NodeWrapper<NodeIds<86, 86, 0>, DataNode_14359aa40>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_143598c90>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359eab0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ec40>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359a590>
		>,
		ParentNode<
			NodeIds<87, 87, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedStandingOnObjectDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedOrientationDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPhysicalVelocityDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedMovementDataNode>,
			ParentNode<
				NodeIds<87, 87, 0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359abd0>,
				NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359ad88>
			>,
			NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CPedSectorPosMapNode>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_1435987e0>,
			NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359a270>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 0>, CPhysicalMigrationDataNode>,
			NodeWrapper<NodeIds<4, 0, 1>, CPhysicalScriptMigrationDataNode>,
			NodeWrapper<NodeIds<4, 4, 1>, CPedTaskSequenceDataNode>
		>
	>
>;
using CWorldStateSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 87, 0>,
		NodeWrapper<NodeIds<87, 87, 0>, CWorldStateBaseDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>
	>
>;
using CWorldProjectileSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		ParentNode<
			NodeIds<1, 0, 0>,
			NodeWrapper<NodeIds<1, 0, 0>, CProjectileCreationDataNode>
		>,
		ParentNode<
			NodeIds<127, 87, 0>,
			NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
			NodeWrapper<NodeIds<87, 87, 0>, CProjectileAttachNode>
		>,
		ParentNode<
			NodeIds<87, 87, 0>,
			NodeWrapper<NodeIds<87, 87, 0>, CWorldProjectileDataNode>
		>,
		ParentNode<
			NodeIds<4, 0, 0>,
			NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>
		>
	>
>;
using CIncidentSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<1, 0, 0>, CIncidentCreateDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435931b8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143593348>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435934d8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143593668>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435937f8>,
		NodeWrapper<NodeIds<87, 87, 0>, CEntityScriptInfoDataNode>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>
	>
>;
using CGuardzoneSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<1, 0, 0>, CGuardzoneCreateDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595740>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435958d0>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595a60>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595a60>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595a60>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595a60>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595bf0>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595bf0>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595bf0>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143595bf0>,
		NodeWrapper<NodeIds<87, 87, 0>, CEntityScriptInfoDataNode>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>
	>
>;
using CPedGroupSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<1, 0, 0>, CPedGroupCreateDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435929e0>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143592b70>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143592e90>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_143592d00>,
		NodeWrapper<NodeIds<127, 127, 0>, CEntityScriptInfoDataNode>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>
	>
>;
using CCombatDirectorSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359bd58>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359bef0>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359c080>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359c210>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_14359c3a0>,
		NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
		NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>
	>
>;
using CPedSharedTargetingSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_1435a1e78>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_1435a2010>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_1435a21a0>,
		NodeWrapper<NodeIds<87, 87, 0>, DataNode_1435a2330>,
		NodeWrapper<NodeIds<87, 87, 0>, CSectorDataNode>,
		NodeWrapper<NodeIds<87, 87, 0>, CSectorPositionDataNode>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>
	>
>;
using CPersistentSyncTree = SyncTree<
	ParentNode<
		NodeIds<127, 0, 0>,
		NodeWrapper<NodeIds<1, 0, 0>, DataNode_1435a24c0>,
		NodeWrapper<NodeIds<127, 127, 0>, DataNode_1435a2658>,
		NodeWrapper<NodeIds<4, 0, 0>, CMigrationDataNode>,
		NodeWrapper<NodeIds<127, 127, 0>, CGlobalFlagsDataNode>
	>
>;
}
