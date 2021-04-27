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

private:
	template<typename TMember>
	inline static TMember get_member(void* ptr)
	{
		union member_cast
		{
			TMember function;
			struct
			{
				void* ptr;
				uintptr_t off;
			};
		};

		member_cast cast;
		cast.ptr = ptr;
		cast.off = 0;

		return cast.function;
	}

public:

#define FORWARD_FUNC(name, offset, ...) \
	using TFn = decltype(&fwEntity::name); \
	void** vtbl = *(void***)(this); \
	return (this->*(get_member<TFn>(vtbl[(offset / 8)])))(__VA_ARGS__);

public:
	inline float GetRadius()
	{
		FORWARD_FUNC(GetRadius, 0x200);
	}

public:
	inline const Matrix4x4& GetTransform() const
	{
		return m_transform;
	}
	
	inline Vector3 GetPosition() const
	{
		return Vector3(
			m_bbMin.x + (m_bbMax.x - m_bbMin.x) / 2,
			m_bbMin.y + (m_bbMax.y - m_bbMin.y) / 2,
			m_bbMin.z + (m_bbMax.z - m_bbMin.z) / 2
		);
	}

	inline void* GetNetObject() const
	{
		static_assert(offsetof(fwEntity, m_netObject) == 224, "wrong GetNetObject");
		return m_netObject;
	}

	inline uint8_t GetType() const
	{
		return m_entityType;
	}

private:
	char m_pad[40]; // +8
	uint8_t m_entityType; // +48
	char m_pad2[15]; // +49
	Matrix4x4 m_transform; // +64
	char m_pad3[96]; // +128
	void* m_netObject; // +224
	char m_pad4[72]; // +232
	Vector3 m_bbMin; // +304
	char m_pad5[4]; // +316
	Vector3 m_bbMax; // +320
};
