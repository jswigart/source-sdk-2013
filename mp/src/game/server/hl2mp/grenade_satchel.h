//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Satchel Charge
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#ifndef	SATCHEL_H
#define	SATCHEL_H

#ifdef _WIN32
#pragma once
#endif

#include "basegrenade_shared.h"
#include "hl2mp/weapon_slam.h"

class CSoundPatch;
class CSprite;

class CSatchelCharge : public CBaseGrenade
{
public:
	DECLARE_CLASS( CSatchelCharge, CBaseGrenade );

	void			Spawn( void );
	void			Precache( void );
	void			BounceSound( void );
	void			SatchelTouch( CBaseEntity *pOther );
	void			SatchelThink( void );
	
	// Input handlers
	void			InputExplode( inputdata_t &inputdata );

	float			m_flNextBounceSoundTime;
	bool			m_bInAir;
	Vector			m_vLastPosition;

#if(USE_OMNIBOT)
	bool GetOmnibotEntityType( EntityInfo& classInfo ) const
	{
		BaseClass::GetOmnibotEntityType( classInfo );

		classInfo.mGroup = ENT_GRP_PROJECTILE;
		classInfo.mClassId = HL2DM_CLASSEX_SATCHEL;

		classInfo.mCategory.SetFlag( ENT_CAT_PROJECTILE );
		return true;
	}
#endif

public:
	CWeapon_SLAM*	m_pMyWeaponSLAM;	// Who shot me..
	bool			m_bIsAttached;
	void			Deactivate( void );

	CSatchelCharge();
	~CSatchelCharge();

	DECLARE_DATADESC();

private:
	void				CreateEffects( void );
	CHandle<CSprite>	m_hGlowSprite;
};

#endif	//SATCHEL_H
