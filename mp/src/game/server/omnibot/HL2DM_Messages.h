////////////////////////////////////////////////////////////////////////////////
//
// $LastChangedBy: jswigart@gmail.com $
// $LastChangedDate: 2013-03-07 21:15:21 -0600 (Thu, 07 Mar 2013) $
// $LastChangedRevision: 837 $
//
// Title: MC Message Structure Definitions
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __HL2DM_MESSAGES_H__
#define __HL2DM_MESSAGES_H__

#include "Base_Messages.h"
#include "HL2DM_Config.h"

#pragma pack(push)
#pragma pack(4)

//////////////////////////////////////////////////////////////////////////

// struct: HL2DM_CanPhysPickup
struct HL2DM_CanPhysPickup
{
	GameEntity	mEntity;
	obBool		mCanPickUp;
};

// struct: HL2DM_PhysGunInfo
struct HL2DM_PhysGunInfo
{
	GameEntity	mHeldEntity;
	float		mLaunchSpeed;
};

struct HL2DM_ChargerStatus
{
	float		mCurrentCharge;
	float		mMaxCharge;
};

#pragma pack(pop)

#endif
