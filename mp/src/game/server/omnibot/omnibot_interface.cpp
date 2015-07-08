#include "cbase.h"
#include "in_buttons.h"
#include "hl2mp_player.h"
#include "world.h"
#include "filesystem.h"
#include "movevars_shared.h"
#include "vprof.h"
#include "ammodef.h"
#include "func_ladder.h"
#include "weapon_physcannon.h"

#include "BotExports.h"
#include "omnibot_interface.h"
#include "omnibot_eventhandler.h"
#include "mathlib\polyhedron.h"

#include <sstream>

inline void ConvertBit( int & srcValue, int & dstValue, int matchBit, int toBit )
{
	if ( srcValue & matchBit )
	{
		dstValue |= toBit;
		srcValue &= ~matchBit; // so we can debug bits we dont handle
	}
}

inline Vector Convert( const obVec3 & vec )
{
	return Vector( vec.x, vec.y, vec.z );
}

enum
{
	MAX_MODELS = 512
};
unsigned short	m_DeletedMapModels[ MAX_MODELS ] = {};
int				m_NumDeletedMapModels = 0;

//////////////////////////////////////////////////////////////////////////
extern IServerPluginHelpers *serverpluginhelpers;
//////////////////////////////////////////////////////////////////////////

ConVar	omnibot_enable( "omnibot_enable", "1", FCVAR_ARCHIVE | FCVAR_PROTECTED );
ConVar	omnibot_path( "omnibot_path", "omni-bot", FCVAR_ARCHIVE | FCVAR_PROTECTED );

bool gStarted = false;

#define OMNIBOT_MODNAME "Half-life 2"

//////////////////////////////////////////////////////////////////////////
// forward decls
CWorld* GetWorldEntity();

//////////////////////////////////////////////////////////////////////////

void Omnibot_Load_PrintMsg( const char *_msg )
{
	Msg( "Omni-bot Loading: %s\n", _msg );
}

void Omnibot_Load_PrintErr( const char *_msg )
{
	Warning( "Omni-bot Loading: %s\n", _msg );
}

//////////////////////////////////////////////////////////////////////////

CON_COMMAND( bot, "Omni-Bot Commands" )
{
	omnibot_interface::OmnibotCommand( args );
}

const int obUtilGetBotTeamFromGameTeam( int _faction )
{
	switch ( _faction )
	{
		case TEAM_COMBINE:
			return HL2DM_TEAM_COMBINE;
		case TEAM_REBELS:
			return HL2DM_TEAM_REBELS;		
	}
	return HL2DM_TEAM_NONE;
}

const int obUtilGetGameTeamFromBotTeam( int _faction )
{
	switch ( _faction )
	{
		case HL2DM_TEAM_COMBINE:
			return TEAM_COMBINE;
		case HL2DM_TEAM_REBELS:
			return TEAM_REBELS;
	}
	return TEAM_UNASSIGNED;
}

struct WeaponEnum
{
	const char *	name;
	HL2DM_Weapon	id;
};

const WeaponEnum gWeapons[] =
{
	{ "weapon_crowbar", HL2DM_WP_CROWBAR },
	{ "weapon_physcannon", HL2DM_WP_GRAVGUN },
	{ "weapon_stunstick", HL2DM_WP_STUNSTICK },
	{ "weapon_pistol", HL2DM_WP_PISTOL },
	{ "weapon_357", HL2DM_WP_REVOLVER },
	{ "weapon_smg1", HL2DM_WP_SMG },
	{ "weapon_shotgun", HL2DM_WP_SHOTGUN },
	{ "weapon_slam", HL2DM_WP_SLAM },
	{ "weapon_rpg", HL2DM_WP_RPG },
	{ "weapon_frag", HL2DM_WP_GRENADE },
	{ "weapon_crossbow", HL2DM_WP_CROSSBOW },
	{ "weapon_ar2", HL2DM_WP_AR2 },
	{ "weapon_cguard", HL2DM_WP_COMBINEGUARD },
	{ "weapon_flaregun", HL2DM_WP_FLAREGUN },
	{ "weapon_annabelle", HL2DM_WP_ANNABELLE },
	{ "weapon_bugbait", HL2DM_WP_BUGBAIT },
};
const size_t gNumWeapons = ARRAYSIZE( gWeapons );

int obUtilGetWeaponId( const char *_weaponName )
{
	if ( _weaponName )
	{
		for ( int i = 0; i < gNumWeapons; ++i )
		{
			if ( !Q_strcmp( gWeapons[ i ].name, _weaponName ) )
				return gWeapons[ i ].id;
		}
	}
	return HL2DM_WP_NONE;
}

const char *obUtilGetStringFromWeaponId( int _weaponId )
{
	for ( int i = 0; i < gNumWeapons; ++i )
	{
		if ( gWeapons[ i ].id == _weaponId )
			return gWeapons[ i ].name;
	}
	return 0;
}

edict_t* INDEXEDICT( int iEdictNum )
{
	return engine->PEntityOfEntIndex( iEdictNum );
}

int ENTINDEX( const edict_t *pEdict )
{
	return engine->IndexOfEdict( pEdict );
}

void NormalizeAngles( QAngle& angles )
{
	int i;

	// Normalize angles to -180 to 180 range
	for ( i = 0; i < 3; i++ )
	{
		if ( angles[ i ] > 180.0 )
		{
			angles[ i ] -= 360.0;
		}
		else if ( angles[ i ] < -180.0 )
		{
			angles[ i ] += 360.0;
		}
	}
}

CBaseEntity *EntityFromHandle( GameEntity _ent )
{
	if ( !_ent.IsValid() )
		return NULL;
	CBaseHandle hndl( _ent.GetIndex(), _ent.GetSerial() );
	CBaseEntity *entity = CBaseEntity::Instance( hndl );
	return entity;
}

GameEntity HandleFromEntity( CBaseEntity *_ent )
{
	if ( _ent )
	{
		const CBaseHandle &hndl = _ent->GetRefEHandle();
		return GameEntity( hndl.GetEntryIndex(), hndl.GetSerialNumber() );
	}
	else
		return GameEntity();
}
GameEntity HandleFromEntity( edict_t *_ent )
{
	return HandleFromEntity( CBaseEntity::Instance( _ent ) );
}

//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////

void Bot_Event_EntityCreated( CBaseEntity *pEnt )
{
	if ( pEnt && IsOmnibotLoaded() )
	{
		Event_EntityCreated d;
		d.mEntity = HandleFromEntity( pEnt );
		if ( SUCCESS( gGameFunctions->GetEntityInfo( d.mEntity, d.mEntityInfo ) ) )
		{
			gBotFunctions->SendGlobalEvent( MessageHelper( GAME_ENTITYCREATED, &d, sizeof( d ) ) );
		}
	}
}

void Bot_Event_EntityDeleted( CBaseEntity *pEnt )
{
	if ( pEnt && IsOmnibotLoaded() )
	{
		if ( pEnt->GetCollideable() && pEnt->GetCollideable()->GetCollisionModelIndex() != -1 )
		{
			string_t mdlName = pEnt->GetModelName();
			if ( mdlName.ToCStr() && mdlName.ToCStr()[ 0 ] == '*' )
			{
				m_DeletedMapModels[ m_NumDeletedMapModels++ ] = atoi( &mdlName.ToCStr()[ 1 ] );
			}
		}

		Event_EntityDeleted d;
		d.mEntity = HandleFromEntity( pEnt );
		gBotFunctions->SendGlobalEvent( MessageHelper( GAME_ENTITYDELETED, &d, sizeof( d ) ) );
	}
}

//////////////////////////////////////////////////////////////////////////

class HL2Interface : public IEngineInterface
{
public:
	int AddBot( const MessageHelper &_data )
	{
		OB_GETMSG( Msg_Addbot );

		int iClientNum = -1;

		edict_t *pEdict = engine->CreateFakeClient( pMsg->mName );
		if ( !pEdict )
		{
			PrintError( "Unable to Add Bot!" );
			return -1;
		}

		// Allocate a player entity for the bot, and call spawn
		CHL2MP_Player *pPlayer = ToHL2MPPlayer( CBaseEntity::Instance( pEdict ) );
		pPlayer->SetIsOmnibot( true );

		pPlayer->ClearFlags();
		pPlayer->AddFlag( FL_CLIENT | FL_FAKECLIENT );

		pPlayer->ChangeTeam( TEAM_UNASSIGNED );
		pPlayer->RemoveAllItems( true );
		pPlayer->Spawn();

		// Get the index of the bot.
		iClientNum = engine->IndexOfEdict( pEdict );
		
		// Success!, return its client num.
		return iClientNum;
	}

	void RemoveBot( const MessageHelper &_data )
	{
		OB_GETMSG( Msg_Kickbot );
		if ( pMsg->mGameId != Msg_Kickbot::InvalidGameId )
		{
			CBasePlayer *ent = UTIL_PlayerByIndex( pMsg->mGameId );
			if ( ent && ent->IsBot() )
			{
				//KickBots[ent->entindex()] = true;
				engine->ServerCommand( UTIL_VarArgs( "kick %s\n", ent->GetPlayerName() ) );
			}
		}
		else
		{
			CBasePlayer *ent = UTIL_PlayerByName( pMsg->mName );
			if ( ent && ent->IsBot() )
			{
				//KickBots[ent->entindex()] = true;
				engine->ServerCommand( UTIL_VarArgs( "kick %s\n", ent->GetPlayerName() ) );
			}
		}
	}

	obResult ChangeTeam( int _client, int _newteam, const MessageHelper *_data )
	{
		edict_t *pEdict = INDEXEDICT( _client );
		if ( pEdict )
		{
			int factionId = obUtilGetGameTeamFromBotTeam( _newteam );
			if ( factionId == TEAM_UNASSIGNED )
			{
				factionId = random->RandomInt( TEAM_COMBINE, TEAM_REBELS );
			}
			serverpluginhelpers->ClientCommand( pEdict, UTIL_VarArgs( "team %d", factionId ) );
			return Success;
		}
		return InvalidEntity;
	}

	obResult ChangeClass( int _client, int _newclass, const MessageHelper *_data )
	{
		edict_t *pEdict = INDEXEDICT( _client );

		if ( pEdict )
		{
			//CBaseEntity *pEntity = CBaseEntity::Instance( pEdict );
			//CHL2MP_Player *pPlayer = ToHL2MPPlayer( pEntity );
			
			return Success;
		}
		return InvalidEntity;
	}


	void UpdateBotInput( int _client, const ClientInput &_input )
	{
		edict_t *pEdict = INDEXENT( _client );
		CBaseEntity *pEntity = pEdict && !FNullEnt( pEdict ) ? CBaseEntity::Instance( pEdict ) : 0;
		CHL2MP_Player *pPlayer = pEntity ? ToHL2MPPlayer( pEntity ) : 0;
		if ( pPlayer && pPlayer->IsBot() )
		{
			CBotCmd cmd;
			//CUserCmd cmd;

			// Process the bot keypresses.
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_ATTACK1 ) )
				cmd.buttons |= IN_ATTACK;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_ATTACK2 ) )
				cmd.buttons |= IN_ATTACK2;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_WALK ) )
				cmd.buttons |= IN_SPEED;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_USE ) )
				cmd.buttons |= IN_USE;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_JUMP ) )
				cmd.buttons |= IN_JUMP;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_CROUCH ) )
				cmd.buttons |= IN_DUCK;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_RELOAD ) )
				cmd.buttons |= IN_RELOAD;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_RESPAWN ) )
				cmd.buttons |= IN_ATTACK;
			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_AIM ) )
				cmd.buttons |= IN_ZOOM;
			
			// Convert the facing vector to angles.
			const QAngle currentAngles = pPlayer->EyeAngles();
			Vector vFacing( _input.mFacing[ 0 ], _input.mFacing[ 1 ], _input.mFacing[ 2 ] );
			VectorAngles( vFacing, cmd.viewangles );
			NormalizeAngles( cmd.viewangles );

			// Any facings that go abive the clamp need to have their yaw fixed just in case.
			if ( cmd.viewangles[ PITCH ] > 89 || cmd.viewangles[ PITCH ] < -89 )
				cmd.viewangles[ YAW ] = currentAngles[ YAW ];

			//cmd.viewangles[PITCH] = clamp(cmd.viewangles[PITCH],-89,89);

			// Calculate the movement vector, taking into account the view direction.
			QAngle angle2d = cmd.viewangles; angle2d.x = 0;

			Vector vForward, vRight, vUp;
			Vector vMoveDir( _input.mMoveDir[ 0 ], _input.mMoveDir[ 1 ], _input.mMoveDir[ 2 ] );
			AngleVectors( angle2d, &vForward, &vRight, &vUp );

			const Vector worldUp( 0.f, 0.f, 1.f );
			cmd.forwardmove = vForward.Dot( vMoveDir ) * pPlayer->MaxSpeed();
			cmd.sidemove = vRight.Dot( vMoveDir ) * pPlayer->MaxSpeed();
			cmd.upmove = worldUp.Dot( vMoveDir ) * pPlayer->MaxSpeed();

			if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_MOVEUP ) )
				cmd.upmove = 127;
			else if ( _input.mButtonFlags.CheckFlag( BOT_BUTTON_MOVEDN ) )
				cmd.upmove = -127;

			if ( cmd.sidemove > 0 )
				cmd.buttons |= IN_MOVERIGHT;
			else if ( cmd.sidemove < 0 )
				cmd.buttons |= IN_MOVELEFT;

			if ( pPlayer->IsOnLadder() )
			{
				if ( cmd.upmove > 0 )
					cmd.buttons |= IN_FORWARD;
				else if ( cmd.upmove < 0 )
					cmd.buttons |= IN_BACK;
			}
			else
			{
				if ( cmd.forwardmove > 0 )
					cmd.buttons |= IN_FORWARD;
				else if ( cmd.forwardmove < 0 )
					cmd.buttons |= IN_BACK;
			}

			// Do we have this weapon?
			const char *pNewWeapon = obUtilGetStringFromWeaponId( _input.mCurrentWeapon );
			CBaseCombatWeapon *pCurrentWpn = pPlayer->GetActiveWeapon();

			if ( pNewWeapon && ( !pCurrentWpn || !FStrEq( pCurrentWpn->GetClassname(), pNewWeapon ) ) )
			{
				CBaseCombatWeapon *pWpn = pPlayer->Weapon_OwnsThisType( pNewWeapon );
				if ( pWpn != pCurrentWpn )
				{
					pPlayer->Weapon_Switch( pWpn );
				}
			}

			pPlayer->GetBotController()->RunPlayerMove( &cmd );
			//pPlayer->ProcessUsercmds(&cmd, 1, 1, 0, false);
			//pPlayer->GetBotController()->PostClientMessagesSent();
		}
	}


	void BotCommand( int _client, const char *_cmd )
	{
		edict_t *pEdict = INDEXENT( _client );
		if ( pEdict && !FNullEnt( pEdict ) )
		{
			serverpluginhelpers->ClientCommand( pEdict, _cmd );
		}
	}

	obBool IsInPVS( const float _pos[ 3 ], const float _target[ 3 ] )
	{
		Vector start( _pos[ 0 ], _pos[ 1 ], _pos[ 2 ] );
		Vector end( _target[ 0 ], _target[ 1 ], _target[ 2 ] );

		byte pvs[ MAX_MAP_CLUSTERS / 8 ];
		int iPVSCluster = engine->GetClusterForOrigin( start );
		int iPVSLength = engine->GetPVSForCluster( iPVSCluster, sizeof( pvs ), pvs );

		return engine->CheckOriginInPVS( end, pvs, iPVSLength ) ? True : False;
	}

	obResult TraceLine( obTraceResult &_result, const float _start[ 3 ], const float _end[ 3 ],
		const AABB *_pBBox, int _mask, int _user, obBool _bUsePVS )
	{
		Vector start( _start[ 0 ], _start[ 1 ], _start[ 2 ] );
		Vector end( _end[ 0 ], _end[ 1 ], _end[ 2 ] );

		byte pvs[ MAX_MAP_CLUSTERS / 8 ];
		int iPVSCluster = engine->GetClusterForOrigin( start );
		int iPVSLength = engine->GetPVSForCluster( iPVSCluster, sizeof( pvs ), pvs );

		bool bInPVS = _bUsePVS ? engine->CheckOriginInPVS( end, pvs, iPVSLength ) : true;
		if ( bInPVS )
		{
			int iMask = 0;
			Ray_t ray;
			trace_t trace;

			CTraceFilterWorldAndPropsOnly filterWorldPropsOnly;

			CBaseEntity *pIgnoreEnt = _user > 0 ? CBaseEntity::Instance( _user ) : 0;
			CTraceFilterSimple filterSimple( pIgnoreEnt, iMask );

			ITraceFilter * traceFilter = &filterSimple;

			// Set up the collision masks
			if ( _mask & TR_MASK_ALL )
				iMask |= MASK_ALL;
			else
			{
				if ( _mask & TR_MASK_SOLID )
					iMask |= MASK_SOLID;
				if ( _mask & TR_MASK_PLAYER )
					iMask |= MASK_PLAYERSOLID;
				if ( _mask & TR_MASK_SHOT )
					iMask |= MASK_SHOT;
				if ( _mask & TR_MASK_OPAQUE )
					iMask |= MASK_OPAQUE;
				if ( _mask & TR_MASK_WATER )
					iMask |= MASK_WATER;
				if ( _mask & TR_MASK_GRATE )
					iMask |= CONTENTS_GRATE;
				if ( _mask & TR_MASK_FLOODFILL )
				{
					traceFilter = &filterWorldPropsOnly;
					iMask |= ( MASK_NPCWORLDSTATIC | CONTENTS_PLAYERCLIP );
				}
				if ( _mask & TR_MASK_FLOODFILLENT )
				{
					iMask |= ( MASK_NPCWORLDSTATIC | CONTENTS_PLAYERCLIP );					
				}
			}

			filterSimple.SetCollisionGroup( iMask );

			// Initialize a ray with or without a bounds
			if ( _pBBox )
			{
				Vector mins( _pBBox->mMins[ 0 ], _pBBox->mMins[ 1 ], _pBBox->mMins[ 2 ] );
				Vector maxs( _pBBox->mMaxs[ 0 ], _pBBox->mMaxs[ 1 ], _pBBox->mMaxs[ 2 ] );
				ray.Init( start, end, mins, maxs );
			}
			else
			{
				ray.Init( start, end );
			}

			enginetrace->TraceRay( ray, iMask, traceFilter, &trace );

			if ( trace.DidHit() && trace.m_pEnt && ( trace.m_pEnt->entindex() != 0 ) )
				_result.mHitEntity = HandleFromEntity( trace.m_pEnt );
			else
				_result.mHitEntity = GameEntity();

			// Fill in the bot traceflag.			
			_result.mFraction = trace.fraction;
			_result.mStartSolid = trace.startsolid;
			_result.mEndpos[ 0 ] = trace.endpos.x;
			_result.mEndpos[ 1 ] = trace.endpos.y;
			_result.mEndpos[ 2 ] = trace.endpos.z;
			_result.mNormal[ 0 ] = trace.plane.normal.x;
			_result.mNormal[ 1 ] = trace.plane.normal.y;
			_result.mNormal[ 2 ] = trace.plane.normal.z;
			_result.mContents = ConvertValue( trace.contents, ConvertContentsFlags, ConvertGameToBot );
			return Success;
		}

		// No Hit or Not in PVS
		_result.mFraction = 0.0f;
		_result.mHitEntity = GameEntity();

		return bInPVS ? Success : OutOfPVS;
	}

	int GetPointContents( const float _pos[ 3 ] )
	{
		const int iContents = UTIL_PointContents( Vector( _pos[ 0 ], _pos[ 1 ], _pos[ 2 ] ) );
		return ConvertValue( iContents, ConvertContentsFlags, ConvertGameToBot );
	}

	virtual int ConvertValue( int value, ConvertType ctype, ConvertDirection cdir )
	{
		if ( cdir == ConvertGameToBot )
		{
			switch ( ctype )
			{
				case ConvertSurfaceFlags:
				{
					// clear flags we don't care about
					value &= ~( SURF_LIGHT | SURF_SKY2D |
						SURF_WARP | SURF_TRANS | SURF_NOPORTAL |
						SURF_NODRAW | SURF_NOLIGHT | SURF_BUMPLIGHT | SURF_NOSHADOWS |
						SURF_NODECALS | SURF_NOCHOP );

					int iBotSurface = 0;
					ConvertBit( value, iBotSurface, SURF_SKY, SURFACE_SKY );
					ConvertBit( value, iBotSurface, SURF_SKIP, SURFACE_IGNORE );
					ConvertBit( value, iBotSurface, SURF_HINT, SURFACE_IGNORE );
					ConvertBit( value, iBotSurface, SURF_NODRAW, SURFACE_NODRAW );
					ConvertBit( value, iBotSurface, SURF_HITBOX, SURFACE_HITBOX );

					assert( value == 0 && "Unhandled flag" );
					return iBotSurface;
				}
				case ConvertContentsFlags:
				{
					value &= ~( CONTENTS_IGNORE_NODRAW_OPAQUE | CONTENTS_AREAPORTAL |
						CONTENTS_AREAPORTAL | CONTENTS_MONSTERCLIP |
						CONTENTS_CURRENT_0 | CONTENTS_CURRENT_90 |
						CONTENTS_CURRENT_180 | CONTENTS_CURRENT_270 |
						CONTENTS_CURRENT_UP | CONTENTS_CURRENT_DOWN |
						CONTENTS_ORIGIN | CONTENTS_MONSTER | CONTENTS_DEBRIS |
						CONTENTS_DETAIL | CONTENTS_TRANSLUCENT | CONTENTS_GRATE |
						CONTENTS_WINDOW | CONTENTS_AUX | LAST_VISIBLE_CONTENTS );

					int iBotContents = 0;
					ConvertBit( value, iBotContents, CONTENTS_SOLID, CONT_SOLID );
					ConvertBit( value, iBotContents, CONTENTS_WATER, CONT_WATER );
					ConvertBit( value, iBotContents, CONTENTS_SLIME, CONT_SLIME );
					ConvertBit( value, iBotContents, CONTENTS_LADDER, CONT_LADDER );
					ConvertBit( value, iBotContents, CONTENTS_MOVEABLE, CONT_MOVER );
					ConvertBit( value, iBotContents, CONTENTS_PLAYERCLIP, CONT_PLYRCLIP );
					ConvertBit( value, iBotContents, CONTENTS_DETAIL, CONT_NONSOLID );
					ConvertBit( value, iBotContents, CONTENTS_TESTFOGVOLUME, CONT_FOG );
					ConvertBit( value, iBotContents, CONTENTS_HITBOX, CONT_HITBOX );

					assert( value == 0 && "Unhandled flag" );
					return iBotContents;
				}
			}
		}
		else
		{
			switch ( ctype )
			{
				case ConvertSurfaceFlags:
				{
					int iBotSurface = 0;
					ConvertBit( value, iBotSurface, SURFACE_SKY, SURF_SKY );
					ConvertBit( value, iBotSurface, SURFACE_IGNORE, SURF_SKIP );
					ConvertBit( value, iBotSurface, SURFACE_NODRAW, SURF_NODRAW );
					ConvertBit( value, iBotSurface, SURFACE_HITBOX, SURF_HITBOX );

					assert( value == 0 && "Unhandled flag" );
					return iBotSurface;
				}
				case ConvertContentsFlags:
				{
					int iBotContents = 0;
					ConvertBit( value, iBotContents, CONT_SOLID, CONTENTS_SOLID );
					ConvertBit( value, iBotContents, CONT_WATER, CONTENTS_WATER );
					ConvertBit( value, iBotContents, CONT_SLIME, CONTENTS_SLIME );
					ConvertBit( value, iBotContents, CONT_LADDER, CONTENTS_LADDER );
					ConvertBit( value, iBotContents, CONT_MOVER, CONTENTS_MOVEABLE );
					ConvertBit( value, iBotContents, CONT_PLYRCLIP, CONTENTS_PLAYERCLIP );
					ConvertBit( value, iBotContents, CONT_NONSOLID, CONTENTS_DETAIL );
					ConvertBit( value, iBotContents, CONT_FOG, CONTENTS_TESTFOGVOLUME );
					ConvertBit( value, iBotContents, CONT_HITBOX, CONTENTS_HITBOX );

					assert( value == 0 && "Unhandled flag" );
					return iBotContents;
				}
			}
		}
		assert( 0 && "Unhandled conversion" );
		return 0;
	}

	obResult GetEntityForMapModel( int mapModelId, GameEntity& entityOut )
	{
		for ( int i = 0; i < m_NumDeletedMapModels; ++i )
		{
			if ( m_DeletedMapModels[ i ] == mapModelId )
			{
				return InvalidEntity;
			}
		}

		for ( CBaseEntity *pEntity = gEntList.FirstEnt(); pEntity != NULL; pEntity = gEntList.NextEnt( pEntity ) )
		{
			string_t mdlName = pEntity->GetModelName();
			if ( mdlName.ToCStr() && mdlName.ToCStr()[ 0 ] == '*' && mapModelId == atoi( &mdlName.ToCStr()[ 1 ] ) )
			{
				entityOut = HandleFromEntity( pEntity->edict() );
				return Success;
			}
		}

		if ( mapModelId == 0 )
		{
			entityOut = HandleFromEntity( GetWorldEntity() );
			return Success;
		}

		return Success;
	}

	obResult GetWorldModel( GameModelInfo & modelOut, MemoryAllocator & alloc )
	{
		Q_snprintf( modelOut.mModelType, sizeof( modelOut.mModelType ), "bsp" );
		Q_snprintf( modelOut.mModelName, sizeof( modelOut.mModelName ), "maps/%s.bsp", GetMapName() );
		return GetModel( modelOut, alloc );
	}

	obResult GetEntityModel( const GameEntity _ent, GameModelInfo & modelOut, MemoryAllocator & alloc )
	{
		CBaseEntity *pEnt = EntityFromHandle( _ent );
		if ( pEnt )
		{
			const string_t mdlName = pEnt->GetModelName();

			Q_strncpy( modelOut.mModelName, mdlName.ToCStr(), sizeof( modelOut.mModelName ) );

			const int len = strlen( modelOut.mModelName );
			for ( int i = 0; i < len; ++i )
			{
				if ( modelOut.mModelName[ i ] == '.' )
					Q_strncpy( modelOut.mModelType, &modelOut.mModelName[ i + 1 ], sizeof( modelOut.mModelType ) );
			}

			if ( modelOut.mModelName[ 0 ] == '*' )
			{
				Q_strncpy( modelOut.mModelType, "submodel", sizeof( modelOut.mModelType ) );
				Q_strncpy( modelOut.mModelName, &modelOut.mModelName[ 1 ], sizeof( modelOut.mModelName ) );
				return Success;
			}

			const SolidType_t entitySolidType = pEnt->CollisionProp() ? pEnt->CollisionProp()->GetSolid() : SOLID_NONE;
			entitySolidType;

			return GetModel( modelOut, alloc );
		}
		return InvalidEntity;
	}

	obResult GetModel( GameModelInfo & modelOut, MemoryAllocator & alloc )
	{
		if ( !Q_stricmp( modelOut.mModelType, "mdl" ) )
		{
			const int modelIndex = engine->PrecacheModel( modelOut.mModelName, true );
			//const int modelIndex = modelinfo->GetModelIndex( modelOut.mModelName );
			const vcollide_t * collide = modelinfo->GetVCollide( modelIndex );
			if ( collide == NULL )
			{
				const model_t* mdl = modelinfo->GetModel( modelIndex );
				if ( mdl )
				{
					Vector mins( FLT_MAX, FLT_MAX, FLT_MAX ), maxs( -FLT_MAX, -FLT_MAX, -FLT_MAX );
					modelinfo->GetModelBounds( mdl, mins, maxs );

					if ( mins.x <= maxs.x && mins.y <= maxs.y && mins.z <= maxs.z )
					{
						modelOut.mAABB.Set( mins.Base(), maxs.Base() );
					}
				}
				return Success;
			}

			std::stringstream str;

			size_t baseVertBuffer = 0;

			for ( int c = 0; c < collide->solidCount; ++c )
			{
				Vector * outVerts;
				const int vertCount = physcollision->CreateDebugMesh( collide->solids[ c ], &outVerts );

				str << "# Vertices " << vertCount << std::endl;
				for ( unsigned short v = 0; v < vertCount; ++v )
				{
					const Vector & vert = outVerts[ v ];
					str << "v " << vert.x << " " << vert.y << " " << vert.z << std::endl;
				}
				str << std::endl;

				const int numTris = vertCount / 3;
				str << "# Faces " << numTris << std::endl;
				for ( unsigned short p = 0; p < numTris; ++p )
				{
					str << "f " <<
						( baseVertBuffer + p * 3 + 1 ) << " " <<
						( baseVertBuffer + p * 3 + 2 ) << " " <<
						( baseVertBuffer + p * 3 + 3 ) << std::endl;
				}
				str << std::endl;

				physcollision->DestroyDebugMesh( vertCount, outVerts );

				baseVertBuffer += vertCount;
			}

			modelOut.mDataBufferSize = str.str().length() + 1;
			modelOut.mDataBuffer = alloc.AllocateMemory( modelOut.mDataBufferSize );
			memset( modelOut.mDataBuffer, 0, modelOut.mDataBufferSize );
			Q_strncpy( modelOut.mDataBuffer, str.str().c_str(), modelOut.mDataBufferSize );
			Q_strncpy( modelOut.mModelType, "obj", sizeof( modelOut.mModelType ) );
			return Success;
		}

		FileHandle_t file = filesystem->Open( modelOut.mModelName, "rb" );
		if ( FILESYSTEM_INVALID_HANDLE != file )
		{
			modelOut.mDataBufferSize = filesystem->Size( file );
			modelOut.mDataBuffer = alloc.AllocateMemory( modelOut.mDataBufferSize );

			filesystem->Read( modelOut.mDataBuffer, modelOut.mDataBufferSize, file );
			filesystem->Close( file );
			return Success;
		}
		return InvalidParameter;
	}

	GameEntity GetLocalGameEntity()
	{
		if ( !engine->IsDedicatedServer() )
		{
			CBasePlayer *localPlayer = UTIL_PlayerByIndex( 1 );
			if ( localPlayer )
				return HandleFromEntity( localPlayer );
		}
		return GameEntity();
	}

	obResult GetEntityInfo( const GameEntity _ent, EntityInfo& classInfo )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			classInfo = EntityInfo();
			if ( pEntity->GetOmnibotEntityType( classInfo ) )
				return Success;
		}
		return InvalidEntity;
	}
	
	obResult GetEntityEyePosition( const GameEntity _ent, float _pos[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			Vector vPos = pEntity->EyePosition();
			_pos[ 0 ] = vPos.x;
			_pos[ 1 ] = vPos.y;
			_pos[ 2 ] = vPos.z;
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityBonePosition( const GameEntity _ent, int _boneid, float _pos[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		CBasePlayer *pPlayer = ToBasePlayer( pEntity );
		if ( pPlayer )
		{
			int iBoneIndex = -1;
			//switch(_boneid)
			//{
			//case BONE_TORSO:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_Spine3");
			//	break;
			//case BONE_PELVIS:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_Hips");
			//	break;
			//case BONE_HEAD:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_Head");
			//	break;
			//case BONE_RIGHTARM:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_RightForeArm");
			//	break;
			//case BONE_LEFTARM:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_LeftForeArm");
			//	break;
			//case BONE_RIGHTHAND:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_RightHand");
			//	break;
			//case BONE_LEFTHAND:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_LeftHand");
			//	break;
			//case BONE_RIGHTLEG:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_RightLeg");
			//	break;
			//case BONE_LEFTLEG:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_LeftLeg");
			//	break;
			//case BONE_RIGHTFOOT:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_RightFoot");
			//	break;
			//case BONE_LEFTFOOT:
			//	iBoneIndex = pPlayer->LookupBone("ffSkel_LeftFoot");
			//	break;
			//	//"ffSg_Yaw"
			//}

			if ( iBoneIndex != -1 )
			{
				Vector vBonePos;
				QAngle boneAngle;

				pPlayer->GetBonePosition( iBoneIndex, vBonePos, boneAngle );

				_pos[ 0 ] = vBonePos.x;
				_pos[ 1 ] = vBonePos.y;
				_pos[ 2 ] = vBonePos.z;

				return Success;
			}
			return InvalidParameter;
		}
		return InvalidEntity;
	}

	obResult GetEntityOrientation( const GameEntity _ent, float _fwd[ 3 ], float _right[ 3 ], float _up[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			QAngle viewAngles = pEntity->EyeAngles();
			AngleVectors( viewAngles, (Vector*)_fwd, (Vector*)_right, (Vector*)_up );
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityVelocity( const GameEntity _ent, float _velocity[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			const Vector &vVelocity = pEntity->GetAbsVelocity();
			_velocity[ 0 ] = vVelocity.x;
			_velocity[ 1 ] = vVelocity.y;
			_velocity[ 2 ] = vVelocity.z;
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityPosition( const GameEntity _ent, float _pos[ 3 ] )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			const Vector &vPos = pEntity->GetAbsOrigin();
			_pos[ 0 ] = vPos.x;
			_pos[ 1 ] = vPos.y;
			_pos[ 2 ] = vPos.z;
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityWorldOBB( const GameEntity _ent, float *_center, float *_axis0, float *_axis1, float *_axis2, float *_extents )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			Vector vMins, vMaxs;

			CBasePlayer *pPlayer = ToBasePlayer( pEntity );
			if ( pPlayer )
			{
				Vector vOrig = pPlayer->GetAbsOrigin();
				vMins = vOrig + pPlayer->GetPlayerMins();
				vMaxs = vOrig + pPlayer->GetPlayerMaxs();
				const Vector center = ( vMins + vMaxs )*0.5f;
				const Vector size = vMaxs - vMins;

				_center[ 0 ] = center.x;
				_center[ 1 ] = center.y;
				_center[ 2 ] = center.z;

				QAngle viewAngles = pEntity->EyeAngles();
				AngleVectors( viewAngles, (Vector*)_axis0, (Vector*)_axis1, (Vector*)_axis2 );

				_extents[ 0 ] = size.x * 0.5f;
				_extents[ 1 ] = size.y * 0.5f;
				_extents[ 2 ] = size.z * 0.5f;
			}
			else
			{
				if ( !pEntity->CollisionProp() || pEntity->entindex() == 0 )
					return InvalidEntity;

				const Vector obbCenter = pEntity->CollisionProp()->OBBCenter();
				const Vector obbSize = pEntity->CollisionProp()->OBBSize();

				_center[ 0 ] = obbCenter.x;
				_center[ 1 ] = obbCenter.y;
				_center[ 2 ] = obbCenter.z;

				QAngle viewAngles = pEntity->EyeAngles();
				AngleVectors( viewAngles, (Vector*)_axis0, (Vector*)_axis1, (Vector*)_axis2 );

				_extents[ 0 ] = obbSize.x;
				_extents[ 1 ] = obbSize.y;
				_extents[ 2 ] = obbSize.z;
			}

			return Success;
		}
		return InvalidEntity;
	}

	obResult GetEntityLocalAABB( const GameEntity _ent, AABB &_aabb )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			Vector vMins, vMaxs;

			CBasePlayer *pPlayer = ToBasePlayer( pEntity );
			if ( pPlayer )
			{
				vMins = pPlayer->GetPlayerMins();
				vMaxs = pPlayer->GetPlayerMaxs();
			}
			else
			{
				if ( !pEntity->CollisionProp() || pEntity->entindex() == 0 )
					return InvalidEntity;

				pEntity->CollisionProp()->WorldSpaceAABB( &vMins, &vMaxs );
			}

			_aabb.mMins[ 0 ] = vMins.x;
			_aabb.mMins[ 1 ] = vMins.y;
			_aabb.mMins[ 2 ] = vMins.z;
			_aabb.mMaxs[ 0 ] = vMaxs.x;
			_aabb.mMaxs[ 1 ] = vMaxs.y;
			_aabb.mMaxs[ 2 ] = vMaxs.z;
			return Success;
		}
		return InvalidEntity;
	}
	obResult GetEntityGroundEntity( const GameEntity _ent, GameEntity &moveent )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			CBaseEntity *pEnt = pEntity->GetGroundEntity();
			if ( pEnt && pEnt != GetWorldEntity() )
				moveent = HandleFromEntity( pEnt );
			return Success;
		}
		return InvalidEntity;
	}

	GameEntity GetEntityOwner( const GameEntity _ent )
	{
		GameEntity owner;

		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			CBaseEntity *pOwner = pEntity->GetOwnerEntity();
			if ( pOwner )
				owner = HandleFromEntity( pOwner );
		}
		return owner;
	}

	int GetEntityTeam( const GameEntity _ent )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			CHL2MP_Player * plyr = ToHL2MPPlayer( pEntity );
			if ( plyr )
			{
				return obUtilGetBotTeamFromGameTeam( plyr->GetTeamNumber() );
			}
			return obUtilGetBotTeamFromGameTeam( plyr->GetTeamNumber() );
		}
		return 0;
	}

	const char *GetEntityName( const GameEntity _ent )
	{
		const char *pName = 0;
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		if ( pEntity )
		{
			CBasePlayer *pPlayer = ToBasePlayer( pEntity );
			if ( pPlayer )
				pName = pPlayer->GetPlayerName();
			else
			{
				pName = pEntity->GetEntityName().ToCStr();
				if ( !pName || pName[ 0 ] == NULL )
				{
					const char * cn = pEntity->GetClassname();
					const CBaseHandle &hndl = pEntity->GetRefEHandle();

					static char buffer[ 64 ];
					pName = buffer;
					Q_snprintf( buffer, 64, "%s_%d_%d", cn ? cn : "", hndl.GetEntryIndex(), hndl.GetSerialNumber() );
				}
			}
		}
		return pName ? pName : "";
	}

	int GetCurrentWeapons( const GameEntity ent, int weaponIds [], int maxWeapons )
	{
		CBaseEntity *pEntity = EntityFromHandle( ent );
		CBasePlayer *pPlayer = ToBasePlayer( pEntity );
		if ( pPlayer )
		{
			int weaponCount = 0;
			for ( int i = 0; i < pPlayer->WeaponCount() && weaponCount < maxWeapons; ++i )
			{
				CBaseCombatWeapon* wpn = pPlayer->GetWeapon( i );
				if ( wpn )
				{
					weaponIds[ weaponCount ] = obUtilGetWeaponId( wpn->GetClassname() );
					if ( weaponIds[ weaponCount ] != HL2DM_WP_NONE )
						++weaponCount;
				}
			}
			return weaponCount;
		}
		return 0;
	}

	obResult GetCurrentWeaponClip( const GameEntity _ent, FireMode _mode, int &_curclip, int &_maxclip )
	{
		CBaseEntity *pEntity = EntityFromHandle( _ent );
		CBasePlayer *pPlayer = ToBasePlayer( pEntity );
		if ( pPlayer )
		{
			CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
			if ( pWeapon )
			{
				_curclip = pWeapon->Clip1();
				_maxclip = pWeapon->GetMaxClip1();
			}
			return Success;
		}
		return InvalidEntity;
	}

	obResult GetCurrentAmmo( const GameEntity _ent, int _weaponId, FireMode _mode, int &_cur, int &_max )
	{
		_cur = 0;
		_max = 0;

		CBaseEntity *pEntity = EntityFromHandle( _ent );
		CHL2MP_Player  *pPlayer = ToHL2MPPlayer( pEntity );
		if ( pPlayer )
		{
			const char *weaponClass = obUtilGetStringFromWeaponId( _weaponId );
			if ( weaponClass )
			{
				CBaseCombatWeapon *pWpn = pPlayer->Weapon_OwnsThisType( weaponClass );
				if ( pWpn )
				{
					//const int iAmmoType = GetAmmoDef()->Index( _mode==Primary?pWpn->GetPrimaryAmmoType():pWpn->GetSecondaryAmmoType());

					_cur = pPlayer->GetAmmoCount( _mode == Primary ? pWpn->GetPrimaryAmmoType() : pWpn->GetSecondaryAmmoType() );
					//_cur = _mode==Primary?pWpn->GetPrimaryAmmoCount():pWpn->GetSecondaryAmmoCount();
					_max = GetAmmoDef()->MaxCarry( _mode == Primary ? pWpn->GetPrimaryAmmoType() : pWpn->GetSecondaryAmmoType() );
				}
			}
			return Success;
		}

		_cur = 0;
		_max = 0;

		return InvalidEntity;
	}

	int GetGameTime()
	{
		return int( gpGlobals->curtime * 1000.0f );
	}

	void GetGoals()
	{
	}

	void GetPlayerInfo( obPlayerInfo &info )
	{
		info.mAvailableTeams = ( 1 << HL2DM_TEAM_COMBINE ) | ( 1 << HL2DM_TEAM_REBELS ); // TEMP
		info.mMaxPlayers = gpGlobals->maxClients;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer	*pEnt = UTIL_PlayerByIndex( i );
			if ( pEnt )
			{
				GameEntity ge = HandleFromEntity( pEnt );
				GetEntityInfo( ge, info.mPlayers[ i ].mEntInfo );
				info.mPlayers[ i ].mTeam = GetEntityTeam( ge );
				info.mPlayers[ i ].mController = pEnt->IsBot() ? obPlayerInfo::Bot : obPlayerInfo::Human;
			}
		}
	}

	obResult InterfaceSendMessage( const MessageHelper &_data, const GameEntity _ent )
	{
		CBaseEntity *pEnt = EntityFromHandle( _ent );
		CHL2MP_Player *pPlayer = ToHL2MPPlayer( pEnt );

#pragma warning(default: 4062) // enumerator 'identifier' in switch of enum 'enumeration' is not handled
		switch ( _data.GetMessageId() )
		{
			///////////////////////
			// General Messages. //
			///////////////////////
			case GEN_MSG_ISALIVE:
			{
				OB_GETMSG( Msg_IsAlive );
				if ( pMsg )
				{
					pMsg->mIsAlive = pEnt && pEnt->IsAlive() && pEnt->GetHealth() > 0 ? True : False;
				}
				break;
			}
			case GEN_MSG_ISRELOADING:
			{
				OB_GETMSG( Msg_Reloading );
				if ( pMsg )
				{
					pMsg->mReloading = pPlayer && pPlayer->IsPlayingGesture( ACT_GESTURE_RELOAD ) ? True : False;
				}
				break;
			}
			case GEN_MSG_ISREADYTOFIRE:
			{
				OB_GETMSG( Msg_ReadyToFire );
				if ( pMsg )
				{
					//CBaseCombatWeapon *pWeapon = pPlayer ? pPlayer->GetActiveWeapon() : 0;
					pMsg->mReady = True;//pWeapon && (pWeapon->mflNextPrimaryAttack <= gpGlobals->curtime) ? True : False;
				}
				break;
			}
			case GEN_MSG_ISALLIED:
			{
				OB_GETMSG( Msg_IsAllied );
				if ( pMsg )
				{
					CBaseEntity *pEntOther = EntityFromHandle( pMsg->mTargetEntity );
					if ( pEnt && pEntOther )
					{						
						pMsg->mIsAllied = g_pGameRules->PlayerRelationship( pEnt, pEntOther ) == GR_TEAMMATE ? True : False;
					}
				}
				break;
			}
			case GEN_MSG_GETEQUIPPEDWEAPON:
			{
				OB_GETMSG( WeaponStatus );
				if ( pMsg )
				{
					CBaseCombatWeapon *pWeapon = pPlayer ? pPlayer->GetActiveWeapon() : 0;
					pMsg->mWeaponId = pWeapon ? obUtilGetWeaponId( pWeapon->GetName() ) : 0;
				}
				break;
			}
			case GEN_MSG_GETMOUNTEDWEAPON:
			{
				break;
			}			
			case GEN_MSG_GETFLAGSTATE:
			{
				OB_GETMSG( Msg_FlagState );
				if ( pMsg )
				{
					/*if(pEnt && pEnt->Classify() == CLASS_INFOSCRIPT)
					{
					CFFInfoScript *pInfoScript = static_cast<CFFInfoScript*>(pEnt);
					if(pInfoScript->IsReturned())
					pMsg->mFlagState = S_FLAG_AT_BASE;
					else if(pInfoScript->IsDropped())
					pMsg->mFlagState = S_FLAG_DROPPED;
					else if(pInfoScript->IsCarried())
					pMsg->mFlagState = S_FLAG_CARRIED;
					else if(pInfoScript->IsRemoved())
					pMsg->mFlagState = S_FLAG_UNAVAILABLE;
					pMsg->mOwner = HandleFromEntity(pEnt->GetOwnerEntity());
					}*/
				}
				break;
			}
			case GEN_MSG_GAMESTATE:
			{
				OB_GETMSG( Msg_GameState );
				if ( pMsg )
				{
					CHL2MPRules * rules = HL2MPRules();
					if ( rules )
					{
						if ( rules->IsIntermission() )
						{
							pMsg->mGameState = GAME_STATE_WAITINGFORPLAYERS;
						}
						else
						{
							pMsg->mGameState = GAME_STATE_PLAYING;
						}
					}

					/*CFFGameRules *pRules = FFGameRules();
					if(pRules)
					{
					pMsg->mTimeLeft = (mp_timelimit.GetFloat() * 60.0f) - gpGlobals->curtime;
					if(pRules->HasGameStarted())
					{
					if(g_fGameOver && gpGlobals->curtime < pRules->mflIntermissionEndTime)
					pMsg->mGameState = GAME_STATE_INTERMISSION;
					else
					pMsg->mGameState = GAME_STATE_PLAYING;
					}
					else
					{
					float flPrematch = pRules->GetRoundStart() + mp_prematch.GetFloat() * 60.0f;
					if( gpGlobals->curtime < flPrematch )
					{
					float flTimeLeft = flPrematch - gpGlobals->curtime;
					if(flTimeLeft < 10)
					pMsg->mGameState = GAME_STATE_WARMUP_COUNTDOWN;
					else
					pMsg->mGameState = GAME_STATE_WARMUP;
					}
					else
					{
					pMsg->mGameState = GAME_STATE_WAITINGFORPLAYERS;
					}
					}
					}*/
				}
				break;
			}
			case GEN_MSG_GETWEAPONLIMITS:
			{
				WeaponLimits *pMsg = _data.Get<WeaponLimits>();
				if ( pMsg )
					pMsg->mLimited = False;
				break;
			}
			case GEN_MSG_GETMAXSPEED:
			{
				OB_GETMSG( Msg_PlayerMaxSpeed );
				if ( pMsg && pPlayer )
				{
					pMsg->mMaxSpeed = pPlayer->MaxSpeed();
				}
				break;
			}
			case GEN_MSG_ENTITYSTAT:
			{
				OB_GETMSG( Msg_EntityStat );
				if ( pMsg )
				{
					if ( pPlayer && !Q_strcmp( pMsg->mStatName, "kills" ) )
						pMsg->mResult = obUserData( pPlayer->FragCount() );
					else if ( pPlayer && !Q_strcmp( pMsg->mStatName, "deaths" ) )
						pMsg->mResult = obUserData( pPlayer->DeathCount() );
					else if ( pPlayer && !Q_strcmp( pMsg->mStatName, "score" ) )
						pMsg->mResult = obUserData( 0 ); // TODO:
				}
				break;
			}
			case GEN_MSG_TEAMSTAT:
			{
				OB_GETMSG( Msg_TeamStat );
				if ( pMsg )
				{
					/*CTeam *pTeam = GetGlobalTeam( obUtilGetGameTeamFromBotTeam(pMsg->mTeam) );
					if(pTeam)
					{
					if(!Q_strcmp(pMsg->mStatName, "score"))
					pMsg->mResult = obUserData(pTeam->GetScore());
					else if(!Q_strcmp(pMsg->mStatName, "deaths"))
					pMsg->mResult = obUserData(pTeam->GetDeaths());
					}*/
				}
				break;
			}
			case GEN_MSG_WPCHARGED:
			{
				OB_GETMSG( WeaponCharged );
				if ( pMsg && pPlayer )
				{
					pMsg->mIsCharged = True;
				}
				break;
			}
			case GEN_MSG_WPHEATLEVEL:
			{
				OB_GETMSG( WeaponHeatLevel );
				if ( pMsg && pPlayer )
				{
					CBaseCombatWeapon *pWp = pPlayer->GetActiveWeapon();
					if ( pWp )
					{
						//pWp->GetHeatLevel(pMsg->mFireMode, pMsg->mCurrentHeat, pMsg->mMaxHeat);
					}
				}
				break;
			}
			case GEN_MSG_ENTITYKILL:
			{
				break;
			}
			case GEN_MSG_SERVERCOMMAND:
			{
				OB_GETMSG( Msg_ServerCommand );
				if ( pMsg && pMsg->mCommand[ 0 ] && sv_cheats->GetBool() )
				{
					const char *cmd = pMsg->mCommand;
					while ( *cmd && *cmd == ' ' )
						++cmd;
					if ( cmd && *cmd )
					{
						engine->ServerCommand( UTIL_VarArgs( "%s\n", cmd ) );
					}
				}
				break;
			}
			case GEN_MSG_PLAYSOUND:
			{
				OB_GETMSG( Event_PlaySound );
				if ( pPlayer )
					pPlayer->EmitSound( pMsg->mSoundName );
				/*else
				FFLib::BroadcastSound(pMsg->mSoundName);*/
				break;
			}
			case GEN_MSG_STOPSOUND:
			{
				OB_GETMSG( Event_StopSound );
				if ( pPlayer )
					pPlayer->StopSound( pMsg->mSoundName );
				/*else
				FFLib::BroadcastSound(pMsg->mSoundName);*/
				break;
			}
			case GEN_MSG_SCRIPTEVENT:
			{
				OB_GETMSG( Event_ScriptEvent );

				/*CFFLuaSC hEvent;
				if(pMsg->mParam1[0])
				hEvent.Push(pMsg->mParam1);
				if(pMsg->mParam2[0])
				hEvent.Push(pMsg->mParam2);
				if(pMsg->mParam3[0])
				hEvent.Push(pMsg->mParam3);

				CBaseEntity *pEnt = NULL;
				if(pMsg->mEntityName[0])
				pEnt = gEntList.FindEntityByName(NULL, pMsg->mEntityName);
				_scriptman.RunPredicates_LUA(pEnt, &hEvent, pMsg->mFunctionName);*/
				break;
			}
			case GEN_MSG_MOVERAT:
			{
				OB_GETMSG( Msg_MoverAt );
				if ( pMsg )
				{
					Vector org(
						pMsg->mPosition[ 0 ],
						pMsg->mPosition[ 1 ],
						pMsg->mPosition[ 2 ] );
					Vector under(
						pMsg->mUnder[ 0 ],
						pMsg->mUnder[ 1 ],
						pMsg->mUnder[ 2 ] );

					trace_t tr;
					unsigned int iMask = MASK_PLAYERSOLID_BRUSHONLY;
					UTIL_TraceLine( org, under, iMask, NULL, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );

					if ( tr.DidHitNonWorldEntity() &&
						!tr.m_pEnt->IsPlayer() &&
						!tr.startsolid )
					{
						pMsg->mEntity = HandleFromEntity( tr.m_pEnt );
					}
				}
				break;
			}			
			//////////////////////////////////
			// Game specific messages next. //
			//////////////////////////////////			
			case HL2DM_MSG_CAN_PHYSPICKUP:
			{
				OB_GETMSG( HL2DM_CanPhysPickup );
				if ( pMsg && pPlayer )
				{
					pMsg->mCanPickUp = False;

					CBaseEntity * pickupObj = EntityFromHandle( pMsg->mEntity );
					if ( pickupObj )
					{
						CBaseCombatWeapon *pWpn = pPlayer->Weapon_OwnsThisType( "weapon_physcannon" );
						if ( pWpn )
						{
							pMsg->mCanPickUp = pWpn->BotCanPickUpObject( pickupObj ) ? True : False;
						}
					}
				}
				break;
			}
			case HL2DM_MSG_PHYSGUNINFO:
			{
				OB_GETMSG( HL2DM_PhysGunInfo );
				if ( pMsg && pPlayer )
				{
					CBaseEntity *heldEnt = 0, *pullEnt = 0;
					Vector vel( 0, 0, 0 );
					float speed = 0.f;

					CBaseCombatWeapon *pWpn = pPlayer->Weapon_OwnsThisType( "weapon_physcannon" );
					if ( pWpn )
					{
						PhysCannonGetInfo( pWpn, heldEnt, pullEnt, speed );
					}
					pMsg->mHeldEntity = HandleFromEntity( heldEnt );
					//pMsg->mPullingEntity = HandleFromEntity(pullEnt);
					pMsg->mLaunchSpeed = speed;
				}
				break;
			}			
			default:
			{
				assert( 0 && "Unknown Interface Message" );
				return InvalidParameter;
			}
		}
#pragma warning(disable: 4062) // enumerator 'identifier' in switch of enum 'enumeration' is not handled
		return Success;
	}

	bool DebugLine( const float _start[ 3 ], const float _end[ 3 ], const obColor &_color, float _time )
	{
		if ( debugoverlay )
		{
			Vector vStart( _start[ 0 ], _start[ 1 ], _start[ 2 ] );
			Vector vEnd( _end[ 0 ], _end[ 1 ], _end[ 2 ] );
			debugoverlay->AddLineOverlay( vStart,
				vEnd,
				_color.r(),
				_color.g(),
				_color.b(),
				false,
				_time );
		}
		return true;
	}

	bool DebugRadius( const float _pos[ 3 ], const float _radius, const obColor &_color, float _time )
	{
		if ( debugoverlay )
		{
			Vector pos( _pos[ 0 ], _pos[ 1 ], _pos[ 2 ] + 4 );
			Vector start;
			Vector end;
			start.Init();
			end.Init();
			start.y += _radius;
			end.y -= _radius;

			float fStepSize = 180.0f / 8.0f;
			for ( int i = 0; i < 8; ++i )
			{
				VectorYawRotate( start, fStepSize, start );
				VectorYawRotate( end, fStepSize, end );

				debugoverlay->AddLineOverlay(
					pos + start,
					pos + end,
					_color.r(),
					_color.g(),
					_color.b(),
					false,
					_time );
			}
		}
		return true;
	}

	bool DebugPolygon( const obVec3 *_verts, const int _numverts, const obColor &_color, float _time, int _flags )
	{
		if ( debugoverlay )
		{
			for ( int i = 2; i < _numverts; i += 3 )
			{
				Vector v0 = Convert( _verts[ 0 ] );
				Vector v1 = Convert( _verts[ i - 1 ] );
				Vector v2 = Convert( _verts[ i - 0 ] );

				debugoverlay->AddTriangleOverlay( v0, v1, v2,
					_color.r(),
					_color.g(),
					_color.b(),
					_color.a(), _flags&IEngineInterface::DR_NODEPTHTEST, _time );

				debugoverlay->AddTriangleOverlay( v0, v2, v1,
					_color.r(),
					_color.g(),
					_color.b(),
					_color.a() * 0.5, _flags&IEngineInterface::DR_NODEPTHTEST, _time );
			}
		}
		return true;
	}

	bool DebugBox( const float _mins[ 3 ], const float _maxs[ 3 ], const obColor &_color, float _time )
	{
		if ( debugoverlay )
		{
			AABB aabb( _mins, _maxs );
			if ( !aabb.IsZero() )
			{
				Vector centerPt;
				aabb.CenterPoint( centerPt.Base() );
				aabb.UnTranslate( centerPt.Base() );
				debugoverlay->AddBoxOverlay2( centerPt,
					Vector( aabb.mMins[ 0 ], aabb.mMins[ 1 ], aabb.mMins[ 2 ] ),
					Vector( aabb.mMaxs[ 0 ], aabb.mMaxs[ 1 ], aabb.mMaxs[ 2 ] ),
					QAngle( 0.f, 0.f, 0.f ),
					Color( 0.f, 0.f, 0.f, 0.f ),
					Color( _color.r(), _color.g(), _color.b(), _color.a() ),
					_time );
			}
		}
		return true;
	}

	void PrintError( const char *_error )
	{
		if ( _error )
			Warning( "%s\n", _error );
	}

	void PrintMessage( const char *_msg )
	{
		if ( _msg )
			Msg( "%s\n", _msg );
	}

	bool PrintScreenText( const float _pos[ 3 ], float _duration, const obColor &_color, const char *_msg )
	{
		if ( _msg && debugoverlay )
		{
			// Handle newlines
			float fVertical = 0.75;
			int LineOffset = 0;
			char buffer[ 1024 ] = {};
			Q_strncpy( buffer, _msg, 1024 );
			char *pbufferstart = buffer;

			int iLength = Q_strlen( buffer );
			for ( int i = 0; i < iLength; ++i )
			{
				if ( buffer[ i ] == '\n' || buffer[ i + 1 ] == '\0' )
				{
					buffer[ i++ ] = 0;
					if ( _pos )
					{
						Vector vPosition( _pos[ 0 ], _pos[ 1 ], _pos[ 2 ] );
						debugoverlay->AddTextOverlay( vPosition, LineOffset++, _duration, pbufferstart );
					}
					else
					{
						debugoverlay->AddScreenTextOverlay( 0.3f, fVertical, _duration,
							_color.r(), _color.g(), _color.b(), _color.a(), pbufferstart );
						fVertical += 0.02f;
					}
					pbufferstart = &buffer[ i ];
				}
			}
		}
		return true;
	}
	const char *GetMapName()
	{
		static char mapname[ 256 ] = { 0 };
		if ( gpGlobals->mapname.ToCStr() )
		{
			Q_snprintf( mapname, sizeof( mapname ), STRING( gpGlobals->mapname ) );
		}
		return mapname;
	}

	void GetMapExtents( AABB &_aabb )
	{
		memset( &_aabb, 0, sizeof( AABB ) );

		CWorld *world = GetWorldEntity();
		if ( world )
		{
			Vector mins, maxs;
			world->GetWorldBounds( mins, maxs );

			for ( int i = 0; i < 3; ++i )
			{
				_aabb.mMins[ i ] = mins[ i ];
				_aabb.mMaxs[ i ] = maxs[ i ];
			}
		}
	}

	GameEntity EntityFromID( const int _gameId )
	{
		CBaseEntity *pEnt = CBaseEntity::Instance( _gameId );
		return HandleFromEntity( pEnt );
	}

	GameEntity EntityByName( const char *_name )
	{
		CBaseEntity *pEnt = _name ? gEntList.FindEntityByName( NULL, _name, NULL ) : NULL;
		return HandleFromEntity( pEnt );
	}

	int IDFromEntity( const GameEntity _ent )
	{
		CBaseEntity *pEnt = EntityFromHandle( _ent );
		return pEnt ? ENTINDEX( pEnt->edict() ) : -1;
	}

	bool DoesEntityStillExist( const GameEntity &_hndl )
	{
		return _hndl.IsValid() ? EntityFromHandle( _hndl ) != NULL : false;
	}

	int GetAutoNavFeatures( AutoNavFeature *_feature, int _max )
	{
		Vector vForward, vRight, vUp;

		int iNumFeatures = 0;

		CBaseEntity *pEnt = gEntList.FirstEnt();
		while ( pEnt )
		{
			_feature[ iNumFeatures ].mEntityInfo = EntityInfo();

			if ( iNumFeatures >= _max )
				return iNumFeatures;

			Vector vPos = pEnt->GetAbsOrigin();
			AngleVectors( pEnt->GetAbsAngles(), &vForward, &vRight, &vUp );
			for ( int j = 0; j < 3; ++j )
			{
				_feature[ iNumFeatures ].mPosition[ j ] = vPos[ j ];
				_feature[ iNumFeatures ].mTargetPosition[ j ] = vPos[ j ];
				_feature[ iNumFeatures ].mFacing[ j ] = vForward[ j ];

				_feature[ iNumFeatures ].mBounds.mMins[ j ] = 0.f;
				_feature[ iNumFeatures ].mBounds.mMaxs[ j ] = 0.f;

				_feature[ iNumFeatures ].mTargetBounds.mMins[ j ] = 0.f;
				_feature[ iNumFeatures ].mTargetBounds.mMaxs[ j ] = 0.f;
			}

			//////////////////////////////////////////////////////////////////////////

			if ( FClassnameIs( pEnt, "info_player_coop" ) ||
				FClassnameIs( pEnt, "info_player_deathmatch" ) ||
				FClassnameIs( pEnt, "info_player_start" ) )
			{
				_feature[ iNumFeatures ].mEntityInfo.mGroup = ENT_GRP_PLAYERSTART;
			}
			else if ( FClassnameIs( pEnt, "trigger_teleport" ) || FClassnameIs( pEnt, "point_teleport" ) )
			{
				CBaseEntity *pTarget = pEnt->GetNextTarget();
				if ( pTarget )
				{
					_feature[ iNumFeatures ].mEntityInfo.mGroup = ENT_GRP_TELEPORTER;
					VectorCopy( pEnt->GetAbsOrigin().Base(), (vec_t*)_feature[ iNumFeatures ].mPosition );
					VectorCopy( pTarget->GetAbsOrigin().Base(), (vec_t*)_feature[ iNumFeatures ].mTargetPosition );

				}
			}
			else if ( CFuncLadder *pLadder = dynamic_cast<CFuncLadder*>( pEnt ) )
			{
				_feature[ iNumFeatures ].mEntityInfo.mGroup = ENT_GRP_LADDER;

				Vector bottom, top;
				pLadder->GetBottomPosition( bottom );
				pLadder->GetTopPosition( top );

				VectorCopy( bottom.Base(), (vec_t*)_feature[ iNumFeatures ].mPosition );
				VectorCopy( top.Base(), (vec_t*)_feature[ iNumFeatures ].mTargetPosition );
			}

			if ( _feature[ iNumFeatures ].mEntityInfo.mGroup != 0 )
			{
				++iNumFeatures;
			}
			pEnt = gEntList.NextEnt( pEnt );
		}
		return iNumFeatures;
	}

	const char *GetGameName()
	{
		return "Halflife 2: Deathmatch";
	}

	const char *GetModName()
	{
		return g_pGameRules ? g_pGameRules->GetGameDescription() : "unknown";
	}

	const char *GetModVers()
	{
		static char buffer[ 256 ];
		engine->GetGameDir( buffer, 256 );
		return buffer;
	}

	const char *GetBotPath()
	{
		return Omnibot_GetLibraryPath();
	}

	const char *GetLogPath()
	{
		static char botPath[ 512 ] = { 0 };

		char buffer[ 512 ] = { 0 };
		g_pFullFileSystem->GetLocalPath(
			UTIL_VarArgs( "%s/%s", omnibot_path.GetString(), "omnibot_hl2dm.dll" ), buffer, 512 );

		Q_ExtractFilePath( buffer, botPath, 512 );
		return botPath;
	}
};

//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------

void omnibot_interface::OnDLLInit()
{
	assert( !g_pEventHandler );
	if ( !g_pEventHandler )
	{
		g_pEventHandler = new omnibot_eventhandler;
		g_pEventHandler->ExtractEvents();
		g_pEventHandler->RegisterEvents();
	}
}

void omnibot_interface::OnDLLShutdown()
{
	if ( g_pEventHandler )
	{
		g_pEventHandler->UnRegisterEvents();
		delete g_pEventHandler;
		g_pEventHandler = 0;
	}
}

//-----------------------------------------------------------------

void omnibot_interface::OmnibotCommand( const CCommand &args )
{
	if ( IsOmnibotLoaded() )
	{
		Arguments botargs; // DrEvil, I had to change this name - Jon
		for ( int i = 0; i < args.ArgC(); ++i )
		{
			Q_strncpy( botargs.mArgs[ botargs.mNumArgs++ ], args[ i ], Arguments::MaxArgLength );
		}
		gBotFunctions->ConsoleCommand( botargs );
	}
	else
		Warning( "Omni-bot Not Loaded\n" );
}

void omnibot_interface::Trigger( CBaseEntity *_ent, CBaseEntity *_activator, const char *_tagname, const char *_action )
{
	if ( IsOmnibotLoaded() )
	{
		TriggerInfo ti;
		ti.mEntity = HandleFromEntity( _ent );
		ti.mActivator = HandleFromEntity( _activator );
		Q_strncpy( ti.mAction, _action, TriggerBufferSize );
		Q_strncpy( ti.mTagName, _tagname, TriggerBufferSize );
		gBotFunctions->SendTrigger( ti );
	}
}
//////////////////////////////////////////////////////////////////////////

class OmnibotEntityListener : public IEntityListener
{
	virtual void OnEntityCreated( CBaseEntity *pEntity )
	{
		Bot_Event_EntityCreated( pEntity );
	}
	virtual void OnEntitySpawned( CBaseEntity *pEntity )
	{
	}
	virtual void OnEntityDeleted( CBaseEntity *pEntity )
	{
		Bot_Event_EntityDeleted( pEntity );
	}
};

OmnibotEntityListener gBotEntityListener;

//////////////////////////////////////////////////////////////////////////
// Interface Functions
void omnibot_interface::LevelInit()
{
}

bool omnibot_interface::InitBotInterface()
{
	if ( !omnibot_enable.GetBool() )
	{
		Msg( "Omni-bot Currently Disabled. Re-enable with cvar omnibot_enable\n" );
		return false;
	}

	/*if( !gameLocal.isServer )
	return false;*/

	Msg( "-------------- Omni-bot Init ----------------\n" );

	//memset(KickBots,0,sizeof(KickBots));

	// Look for the bot dll.
	const int BUF_SIZE = 1024;
	char botFilePath[ BUF_SIZE ] = { 0 };
	char botPath[ BUF_SIZE ] = { 0 };

	filesystem->GetLocalPath(
		UTIL_VarArgs( "%s/%s", omnibot_path.GetString(), "omnibot_hl2dm.dll" ), botFilePath, BUF_SIZE );
	Q_ExtractFilePath( botFilePath, botPath, BUF_SIZE );
	botPath[ strlen( botPath ) - 1 ] = 0;
	Q_FixSlashes( botPath );

	gGameFunctions = new HL2Interface;
	omnibot_error err = Omnibot_LoadLibrary( HL2DM_VERSION_LATEST, "omnibot_hl2dm", Omnibot_FixPath( botPath ) );
	if ( err == BOT_ERROR_NONE )
	{
		gStarted = false;
		gEntList.RemoveListenerEntity( &gBotEntityListener );
		gEntList.AddListenerEntity( &gBotEntityListener );

		// add the initial set of entities
		CBaseEntity * ent = gEntList.FirstEnt();
		while ( ent != NULL )
		{
			Bot_Event_EntityCreated( ent );
			ent = gEntList.NextEnt( ent );
		}
	}
	Msg( "---------------------------------------------\n" );
	return err == BOT_ERROR_NONE;
}

void omnibot_interface::ShutdownBotInterface()
{
	gEntList.RemoveListenerEntity( &gBotEntityListener );
	if ( IsOmnibotLoaded() )
	{
		Msg( "------------ Omni-bot Shutdown --------------\n" );
		Notify_GameEnded( 0 );
		gBotFunctions->Shutdown();
		Omnibot_FreeLibrary();
		Msg( "Omni-bot Shut Down Successfully\n" );
		Msg( "---------------------------------------------\n" );
	}

	// Temp fix?
	if ( debugoverlay )
		debugoverlay->ClearAllOverlays();
}

void omnibot_interface::UpdateBotInterface()
{
	VPROF_BUDGET( "Omni-bot::Update", _T( "Omni-bot" ) );

	if ( IsOmnibotLoaded() )
	{
		static float serverGravity = 0.0f;
		if ( serverGravity != sv_gravity.GetFloat() )
		{
			Event_SystemGravity d = { -sv_gravity.GetFloat() };
			gBotFunctions->SendGlobalEvent( MessageHelper( GAME_GRAVITY, &d, sizeof( d ) ) );
			serverGravity = sv_gravity.GetFloat();
		}
		static bool cheatsEnabled = false;
		if ( sv_cheats->GetBool() != cheatsEnabled )
		{
			Event_SystemCheats d = { sv_cheats->GetBool() ? True : False };
			gBotFunctions->SendGlobalEvent( MessageHelper( GAME_CHEATS, &d, sizeof( d ) ) );
			cheatsEnabled = sv_cheats->GetBool();
		}
		//////////////////////////////////////////////////////////////////////////
		if ( !engine->IsDedicatedServer() )
		{
			for ( int i = 1; i <= gpGlobals->maxClients; i++ )
			{
				CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
				if ( pPlayer && !pPlayer->IsBot() &&
					( pPlayer->GetObserverMode() != OBS_MODE_ROAMING &&
					pPlayer->GetObserverMode() != OBS_MODE_DEATHCAM ) )
				{
					CBasePlayer *pSpectatedPlayer = ToBasePlayer( pPlayer->GetObserverTarget() );
					if ( pSpectatedPlayer )
					{
						omnibot_interface::Notify_Spectated( pPlayer, pSpectatedPlayer );
					}
				}
			}
		}
		//////////////////////////////////////////////////////////////////////////
		if ( !gStarted )
		{
			omnibot_interface::Notify_GameStarted();
			gStarted = true;
		}
		gBotFunctions->Update();
	}
}

//////////////////////////////////////////////////////////////////////////
// Message Helpers
void omnibot_interface::Notify_GameStarted()
{
	if ( !IsOmnibotLoaded() )
		return;

	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_STARTGAME ) );
}

void omnibot_interface::Notify_GameEnded( int _winningteam )
{
	if ( !IsOmnibotLoaded() )
		return;

	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_ENDGAME ) );
}

void omnibot_interface::Notify_ChatMsg( CBasePlayer *_player, const char *_msg )
{
	if ( !IsOmnibotLoaded() )
		return;

	Event_ChatMessage d;
	d.mWhoSaidIt = HandleFromEntity( _player );
	Q_strncpy( d.mMessage, _msg ? _msg : "<unknown>",
		sizeof( d.mMessage ) / sizeof( d.mMessage[ 0 ] ) );
	gBotFunctions->SendGlobalEvent( MessageHelper( PERCEPT_HEAR_GLOBALCHATMSG, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_TeamChatMsg( CBasePlayer *_player, const char *_msg )
{
	if ( !IsOmnibotLoaded() )
		return;

	Event_ChatMessage d;
	d.mWhoSaidIt = HandleFromEntity( _player );
	Q_strncpy( d.mMessage, _msg ? _msg : "<unknown>",
		sizeof( d.mMessage ) / sizeof( d.mMessage[ 0 ] ) );

	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );

		// Check player classes on this player's team
		if ( pPlayer && pPlayer->IsBot() && pPlayer->GetTeamNumber() == _player->GetTeamNumber() )
		{
			gBotFunctions->SendEvent( pPlayer->entindex(),
				MessageHelper( PERCEPT_HEAR_TEAMCHATMSG, &d, sizeof( d ) ) );
		}
	}
}

void omnibot_interface::Notify_Spectated( CBasePlayer *_player, CBasePlayer *_spectated )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_spectated->IsBot() )
		return;

	if ( _spectated && _spectated->IsBot() )
	{
		int iGameId = _spectated->entindex();
		Event_Spectated d = { _player->entindex() - 1 };
		gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_SPECTATED, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_ClientConnected( CBasePlayer *_player, bool _isbot, int _team, int _class )
{
	if ( !IsOmnibotLoaded() )
		return;

	int iGameId = _player->entindex();
	Event_SystemClientConnected d;
	d.mGameId = iGameId;
	d.mIsBot = _isbot ? True : False;
	d.mDesiredTeam = _team;
	d.mDesiredClass = _class;

	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_CLIENTCONNECTED, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_ClientDisConnected( CBasePlayer *_player )
{
	if ( !IsOmnibotLoaded() )
		return;

	int iGameId = _player->entindex();
	Event_SystemClientDisConnected d = { iGameId };
	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_CLIENTDISCONNECTED, &d, sizeof( d ) ) );

}

void omnibot_interface::Notify_Hurt( CBasePlayer *_player, CBaseEntity *_attacker )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_TakeDamage d = { HandleFromEntity( _attacker ) };
	gBotFunctions->SendEvent( iGameId, MessageHelper( PERCEPT_FEEL_PAIN, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_Death( CBasePlayer *_player, CBaseEntity *_attacker, const char *_weapon )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();

	Event_Death d;
	d.mWhoKilledMe = HandleFromEntity( _attacker );
	Q_strncpy( d.mMeansOfDeath, _weapon ? _weapon : "<unknown>", sizeof( d.mMeansOfDeath ) );
	gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_DEATH, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_KilledSomeone( CBasePlayer *_player, CBaseEntity *_victim, const char *_weapon )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_KilledSomeone d;
	d.mWhoIKilled = HandleFromEntity( _victim );
	Q_strncpy( d.mMeansOfDeath, _weapon ? _weapon : "<unknown>", sizeof( d.mMeansOfDeath ) );
	gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_KILLEDSOMEONE, &d, sizeof( d ) ) );

}

void omnibot_interface::Notify_ChangedTeam( CBasePlayer *_player, int _newteam )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_ChangeTeam d = { _newteam };
	gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_CHANGETEAM, &d, sizeof( d ) ) );

}

void omnibot_interface::Notify_ChangedClass( CBasePlayer *_player, int _oldclass, int _newclass )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_ChangeClass d = { _newclass };
	gBotFunctions->SendEvent( iGameId, MessageHelper( MESSAGE_CHANGECLASS, &d, sizeof( d ) ) );

}

void omnibot_interface::Notify_PlayerShoot( CBasePlayer *_player, int _weaponId, CBaseEntity *_projectile )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	int iGameId = _player->entindex();
	Event_WeaponFire d = {};
	d.mWeaponId = _weaponId;
	d.mProjectile = HandleFromEntity( _projectile );
	d.mFireMode = Primary;
	gBotFunctions->SendEvent( iGameId, MessageHelper( ACTION_WEAPON_FIRE, &d, sizeof( d ) ) );
}

void omnibot_interface::Notify_PlayerUsed( CBasePlayer *_player, CBaseEntity *_entityUsed )
{
	if ( !IsOmnibotLoaded() )
		return;
	if ( !_player->IsBot() )
		return;

	CBasePlayer *pUsedPlayer = ToBasePlayer( _entityUsed );
	if ( pUsedPlayer && pUsedPlayer->IsBot() )
	{
		int iGameId = pUsedPlayer->entindex();
		Event_PlayerUsed d = { HandleFromEntity( _player ) };
		gBotFunctions->SendEvent( iGameId, MessageHelper( PERCEPT_FEEL_PLAYER_USE, &d, sizeof( d ) ) );
	}
}

void omnibot_interface::Notify_Sound( CBaseEntity *_source, int _sndtype, const char *_name )
{
	if ( IsOmnibotLoaded() )
	{
		Event_Sound d = {};
		d.mSource = HandleFromEntity( _source );
		d.mSoundType = _sndtype;
		Vector v = _source->GetAbsOrigin();
		d.mOrigin[ 0 ] = v[ 0 ];
		d.mOrigin[ 1 ] = v[ 1 ];
		d.mOrigin[ 2 ] = v[ 2 ];
		Q_strncpy( d.mSoundName, _name ? _name : "<unknown>", sizeof( d.mSoundName ) / sizeof( d.mSoundName[ 0 ] ) );
		gBotFunctions->SendGlobalEvent( MessageHelper( GAME_SOUND, &d, sizeof( d ) ) );
	}
}

//////////////////////////////////////////////////////////////////////////

void omnibot_interface::Notify_ItemRemove( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetEntityName().ToCStr(), "item_removed" );
}
void omnibot_interface::Notify_ItemRestore( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetEntityName().ToCStr(), "item_restored" );
}
void omnibot_interface::Notify_ItemDropped( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetEntityName().ToCStr(), "item_dropped" );
}
void omnibot_interface::Notify_ItemPickedUp( CBaseEntity *_entity, CBaseEntity *_whodoneit )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, _whodoneit, _entity->GetEntityName().ToCStr(), "item_pickedup" );
}
void omnibot_interface::Notify_ItemRespawned( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetEntityName().ToCStr(), "item_respawned" );
}
void omnibot_interface::Notify_ItemReturned( CBaseEntity *_entity )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( _entity, NULL, _entity->GetEntityName().ToCStr(), "item_returned" );
}
void omnibot_interface::Notify_FireOutput( const char *_entityname, const char *_output )
{
	if ( !IsOmnibotLoaded() )
		return;

	CBaseEntity *pEnt = _entityname ? gEntList.FindEntityByName( NULL, _entityname, NULL ) : NULL;
	omnibot_interface::Trigger( pEnt, NULL, _entityname, _output );
}
void omnibot_interface::BotSendTriggerEx( const char *_entityname, const char *_action )
{
	if ( !IsOmnibotLoaded() )
		return;

	omnibot_interface::Trigger( NULL, NULL, _entityname, _action );
}
void omnibot_interface::SendBotSignal( const char *_signal )
{
	if ( !IsOmnibotLoaded() )
		return;

	Event_ScriptSignal d;
	memset( &d, 0, sizeof( d ) );
	Q_strncpy( d.mSignalName, _signal, sizeof( d.mSignalName ) );
	gBotFunctions->SendGlobalEvent( MessageHelper( GAME_SCRIPTSIGNAL, &d, sizeof( d ) ) );
}


//////////////////////////////////////////////////////////////////////////
// Game Events

//////////////////////////////////////////////////////////////////////////
