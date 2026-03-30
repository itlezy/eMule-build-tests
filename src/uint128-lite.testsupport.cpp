#include "kademlia/utils/KadSupport.h"
#include "kademlia/utils/UInt128.h"

namespace Kademlia
{
	/**
	 * Supplies the minimal CUInt128 member implementations needed by Kad helper regression tests.
	 */
	CUInt128& CUInt128::SetValue(const CUInt128 &uValue)
	{
		m_u64Data[0] = uValue.m_u64Data[0];
		m_u64Data[1] = uValue.m_u64Data[1];
		return *this;
	}

	/**
	 * Supplies the minimal CUInt128 scalar assignment used by Kad helper regression tests.
	 */
	CUInt128& CUInt128::SetValue(ULONG uValue)
	{
		m_u64Data[0] = 0;
		m_uData[2] = 0;
		m_uData[3] = uValue;
		return *this;
	}

	/**
	 * Supplies the minimal CUInt128 comparison used by Kad helper regression tests.
	 */
	int CUInt128::CompareTo(const CUInt128 &uOther) const
	{
		for (int iIndex = 0; iIndex < 4; ++iIndex) {
			if (m_uData[iIndex] < uOther.m_uData[iIndex])
				return -1;
			if (m_uData[iIndex] > uOther.m_uData[iIndex])
				return 1;
		}
		return 0;
	}
}
