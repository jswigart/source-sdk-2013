#ifndef __OMNIBOT_INTERFACE_H__
#define __OMNIBOT_INTERFACE_H__

#include "Omni-Bot.h"
#include "HL2DM_Config.h"
#include "HL2DM_Messages.h"

class CBaseCombatCharacter;

class omnibot_interface
{
public:
	static void OnDLLInit();
	static void OnDLLShutdown();

	static void LevelInit();
	static bool InitBotInterface();
	static void ShutdownBotInterface();
	static void UpdateBotInterface();
	static void OmnibotCommand( const CCommand &args = CCommand() );

	static void Trigger( CBaseEntity *_ent, CBaseEntity *_activator, const char *_tagname, const char *_action );

	// Message Helpers
	static void Notify_GameStarted();
	static void Notify_GameEnded( int _winningteam );

	static void Notify_ChatMsg( CBasePlayer *_player, const char *_msg );
	static void Notify_TeamChatMsg( CBasePlayer *_player, const char *_msg );
	static void Notify_Spectated( CBasePlayer *_player, CBasePlayer *_spectated );

	static void Notify_ClientConnected( CBasePlayer *_player, bool _isbot, int _team = RANDOM_TEAM_IF_NO_TEAM, int _class = RANDOM_CLASS_IF_NO_CLASS );
	static void Notify_ClientDisConnected( CBasePlayer *_player );

	static void Notify_Hurt( CBasePlayer *_player, CBaseEntity *_attacker );
	static void Notify_Death( CBasePlayer *_player, CBaseEntity *_attacker, const char *_weapon );
	static void Notify_KilledSomeone( CBasePlayer *_player, CBaseEntity *_victim, const char *_weapon );

	static void Notify_Infected( CBasePlayer *_target, CBasePlayer *_infector );
	static void Notify_Cured( CBasePlayer *_target, CBasePlayer *_infector );
	static void Notify_BurnLevel( CBasePlayer *_target, CBasePlayer *_burner, int _burnlevel );

	static void Notify_ChangedTeam( CBasePlayer *_player, int _newteam );
	static void Notify_ChangedClass( CBasePlayer *_player, int _oldclass, int _newclass );

	static void Notify_PlayerShoot( CBasePlayer *_player, int _weapon, CBaseEntity *_projectile );
	static void Notify_PlayerUsed( CBasePlayer *_player, CBaseEntity *_entityUsed );

	static void Notify_Sound( CBaseEntity *_source, int _sndtype, const char *_name );

	static void Notify_ItemRemove( CBaseEntity *_entity );
	static void Notify_ItemRestore( CBaseEntity *_entity );
	static void Notify_ItemDropped( CBaseEntity *_entity );
	static void Notify_ItemPickedUp( CBaseEntity *_entity, CBaseEntity *_whodoneit );
	static void Notify_ItemRespawned( CBaseEntity *_entity );
	static void Notify_ItemReturned( CBaseEntity *_entity );

	static void Notify_FireOutput( const char *_entityname, const char *_output );

	//////////////////////////////////////////////////////////////////////////
	// MC specific events

	static void BotSendTriggerEx( const char *_entityname, const char *_action );
	static void SendBotSignal( const char *_signal );
};
#endif
