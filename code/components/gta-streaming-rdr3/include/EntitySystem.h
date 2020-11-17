#pragma once

#include <directxmath.h>

#ifdef COMPILING_GTA_STREAMING_RDR3
#define STREAMING_EXPORT DLL_EXPORT
#else
#define STREAMING_EXPORT DLL_IMPORT
#endif

using Vector3 = DirectX::XMFLOAT3;
using Matrix4x4 = DirectX::XMFLOAT4X4;

class fwEntity;

namespace rage
{
	class STREAMING_EXPORT fwRefAwareBase
	{
	public:
		~fwRefAwareBase() = default;

	public:
		void AddKnownRef(void** ref) const;

		void RemoveKnownRef(void** ref) const;
	};

	class STREAMING_EXPORT fwScriptGuid
	{
	public:
		static fwEntity* GetBaseFromGuid(int handle);
	};

	using fwEntity = ::fwEntity;
}


class STREAMING_EXPORT fwEntity : public rage::fwRefAwareBase
{
public:
	virtual ~fwEntity() = default;

	virtual bool IsOfType(uint32_t hash) = 0;

public:
	inline void* GetNetObject() const
	{
		static_assert(offsetof(fwEntity, m_netObject) == 224, "wrong GetNetObject");
		return m_netObject;
	}

private:
	char m_pad[40]; // +8
	uint8_t m_entityType; // +48
	char m_pad2[175]; // +49
	void* m_netObject; // +224
};
