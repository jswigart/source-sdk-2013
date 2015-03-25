#include "cbase.h"
#include "ai_basenpc.h"

#include "omnibot_interface.h"
#include "omnibot_eventhandler.h"

#include <filesystem.h>

KeyValues *g_pGameEvents = NULL;
KeyValues *g_pEngineEvents = NULL;
KeyValues *g_pModEvents = NULL;

ConVar g_BotShowEvents( "omnibot_showevents", "", 0, "Echo Recieved Events to console." );

omnibot_eventhandler *g_pEventHandler = NULL;

typedef void (*pfnEventCallback)(IGameEvent *_event);

typedef struct  
{
	const char *		m_EventName;
	pfnEventCallback	m_Callback;
} EventCallback;

void event_ServerMessage(IGameEvent *_event);
void event_ServerPlayerConnect(IGameEvent *_event);
void event_ServerPlayerDisConnect(IGameEvent *_event);
void event_ServerPlayerInfo(IGameEvent *_event);
void event_ServerPlayerActivate(IGameEvent *_event);
void event_ServerPlayerSay(IGameEvent *_event);
void event_ServerPlayerSayTeam(IGameEvent *_event);

void event_GameTeamInfo(IGameEvent *_event);
void event_GameTeamScore(IGameEvent *_event);
void event_GamePlayerScore(IGameEvent *_event);
//void event_GamePlayerShoot(IGameEvent *_event);
void event_GamePlayerUse(IGameEvent *_event);
void event_GamePlayerChangeName(IGameEvent *_event);
void event_GameNewMap(IGameEvent *_event);
void event_GameStart(IGameEvent *_event);
void event_GameEnd(IGameEvent *_event);
void event_RoundStart(IGameEvent *_event);
void event_RoundEnd(IGameEvent *_event);
void event_GameMessage(IGameEvent *_event);
void event_BreakBreakable(IGameEvent *_event);
void event_BreakProp(IGameEvent *_event);
void event_Class(IGameEvent *_event);
void event_Team(IGameEvent *_event);
void event_Hurt(IGameEvent *_event);
void event_Death(IGameEvent *_event);
void event_Spawn(IGameEvent *_event);

const int obUtilGetBotTeamFromGameTeam( int _faction );

const EventCallback EVENT_CALLBACKS[] =
{
	// Engine Events
	{ "server_message", event_ServerMessage },
	{ "player_connect", event_ServerPlayerConnect },
	{ "player_disconnect", event_ServerPlayerDisConnect },
	{ "player_info", event_ServerPlayerInfo },
	{ "player_activate", event_ServerPlayerActivate },
	{ "player_say", event_ServerPlayerSay },
	{ "player_sayteam", event_ServerPlayerSayTeam },
	
	// Game Events
	{ "team_info", event_GameTeamInfo },
	{ "team_score", event_GameTeamScore },	
	{ "player_score", event_GamePlayerScore },
	//{ "player_shoot", event_GamePlayerShoot },
	{ "player_use", event_GamePlayerUse },
	{ "player_changename", event_GamePlayerChangeName },
	{ "game_newmap", event_GameNewMap },
	//{ "game_start", event_GameStart },
	{ "game_end", event_GameEnd },
	{ "round_start", event_RoundStart },
	{ "round_end", event_RoundEnd },
	{ "game_message", event_GameMessage },
	{ "break_breakable", event_BreakBreakable },
	{ "break_prop", event_BreakProp },

	// Mod Events
	{ "player_changeclass", event_Class },
	{ "player_team", event_Team },
	//{ "player_hurt", event_Hurt },
	//{ "player_death", event_Death },
	//{ "player_spawn", event_Spawn },
};

//---------------------------------------------------------------------------------
// Purpose: called when an event is fired
//---------------------------------------------------------------------------------

void omnibot_eventhandler::RegisterEvents()
{
	int iNumCallbacks = sizeof(EVENT_CALLBACKS) / sizeof(EVENT_CALLBACKS[0]);
	for(int i = 0; i < iNumCallbacks; ++i)
	{
		ListenForGameEvent( EVENT_CALLBACKS[i].m_EventName );
	}
}

void omnibot_eventhandler::UnRegisterEvents()
{
}

void omnibot_eventhandler::FireGameEvent( IGameEvent *_event )
{
	// do we want to display game events on the fly ?
	if(g_BotShowEvents.GetBool())
		PrintEvent(_event);

	// Dispatch game events as bot messages where needed
	if(_event)
	{
		int iNumCallbacks = sizeof(EVENT_CALLBACKS) / sizeof(EVENT_CALLBACKS[0]);
		for(int i = 0; i < iNumCallbacks; ++i)
		{
			if(!Q_strcmp(_event->GetName(), EVENT_CALLBACKS[i].m_EventName))
			{
				EVENT_CALLBACKS[i].m_Callback(_event);
				break;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void omnibot_eventhandler::ExtractEvents()
{
	// read the game events
	g_pGameEvents = new KeyValues("GameEventsFile");
	if (g_pGameEvents->LoadFromFile(filesystem, "resource/GameEvents.res", "GAME"))
	{
		// loop through all events in this file and record ourselves as listener for each
		for (KeyValues *pEvent = g_pGameEvents->GetFirstSubKey(); pEvent; pEvent = pEvent->GetNextKey())
		{
			//DevMsg("Event: %s\n", pEvent->GetName());
			ListenForGameEvent( pEvent->GetName() );
		}
	}
	else
		DevMsg ("Unable to read game events from file\n");
	// read the engine events
	g_pEngineEvents = new KeyValues("EngineEventsFile");
	if (g_pEngineEvents->LoadFromFile(filesystem, "resource/ServerEvents.res", "GAME"))
	{
		// loop through all events in this file and record ourselves as listener for each
		for (KeyValues *pEvent = g_pEngineEvents->GetFirstSubKey(); pEvent; pEvent = pEvent->GetNextKey())
		{
			//DevMsg("Event: %s\n", pEvent->GetName());
			ListenForGameEvent( pEvent->GetName() );
		}
	}
	else
		DevMsg ("Unable to read engine events from file\n");
	// read the MOD events
	g_pModEvents = new KeyValues("ModEventsFile");
	if (g_pModEvents->LoadFromFile(filesystem, "resource/ModEvents.res", "MOD"))
	{
		// loop through all events in this file and record ourselves as listener for each
		for (KeyValues *pEvent = g_pModEvents->GetFirstSubKey(); pEvent; pEvent = pEvent->GetNextKey())
		{
			//DevMsg("Event: %s\n", pEvent->GetName());
			ListenForGameEvent( pEvent->GetName() );
		}
	}
	else
		DevMsg ("Unable to read MOD events from file\n");
}

void omnibot_eventhandler::PrintEvent(IGameEvent *pEvent)
{
	if(!pEvent)
		return;

	KeyValues *pEventAsKey;
	KeyValues *pKey;
	DevMsg ("Got event \"%s\"\n", pEvent->GetName ()); // event name
	DevMsg ("{\n"); // print the open brace
	// find the key/value corresponding to this event
	pEventAsKey = NULL;
	// look in the game events first...
	if (pEventAsKey == NULL)
		for (pEventAsKey = g_pGameEvents->GetFirstSubKey (); pEventAsKey; pEventAsKey = pEventAsKey->GetNextKey ())
			if (strcmp (pEventAsKey->GetName (), pEvent->GetName ()) == 0)
				break;
	// if not found, then look in the engine events...
	if (pEventAsKey == NULL)
		for (pEventAsKey = g_pEngineEvents->GetFirstSubKey (); pEventAsKey; pEventAsKey = pEventAsKey->GetNextKey ())
			if (strcmp (pEventAsKey->GetName (), pEvent->GetName ()) == 0)
				break;
	// and finally look for it in the mod events
	if (pEventAsKey == NULL)
		for (pEventAsKey = g_pModEvents->GetFirstSubKey (); pEventAsKey; pEventAsKey = pEventAsKey->GetNextKey ())
			if (strcmp (pEventAsKey->GetName (), pEvent->GetName ()) == 0)
				break;
	// display the whole key/value tree for this event
	for (pKey = pEventAsKey->GetFirstSubKey (); pKey; pKey = pKey->GetNextKey ())
	{
		// given the data type, print out the data
		if (strcmp (pKey->GetString (), "none") == 0)
			DevMsg ("   \"%s\" = no value (TYPE_NONE)\n", pKey->GetName ());
		else if (strcmp (pKey->GetString (), "string") == 0)
			DevMsg ("   \"%s\" = \"%s\" (TYPE_STRING)\n", pKey->GetName (), pEvent->GetString (pKey->GetName ()));
		else if (strcmp (pKey->GetString (), "bool") == 0)
			DevMsg ("   \"%s\" = %s (TYPE_BOOL)\n", pKey->GetName (), (pEvent->GetBool (pKey->GetName ()) ? "true" : "false"));
		else if (strcmp (pKey->GetString (), "byte") == 0)
			DevMsg ("   \"%s\" = %d (TYPE_BYTE)\n", pKey->GetName (), pEvent->GetInt (pKey->GetName ()));
		else if (strcmp (pKey->GetString (), "short") == 0)
			DevMsg ("   \"%s\" = %d (TYPE_SHORT)\n", pKey->GetName (), pEvent->GetInt (pKey->GetName ()));
		else if (strcmp (pKey->GetString (), "long") == 0)
			DevMsg ("   \"%s\" = %d (TYPE_LONG)\n", pKey->GetName (), pEvent->GetInt (pKey->GetName ()));
		else if (strcmp (pKey->GetString (), "float") == 0)
			DevMsg ("   \"%s\" = %f (TYPE_FLOAT)\n", pKey->GetName (), pEvent->GetFloat (pKey->GetName ()));
	}
	DevMsg ("}\n");
}

void omnibot_eventhandler::PrintEventStructure(KeyValues *pEventAsKey)
{
	KeyValues *pKey;
	DevMsg ("Event \"%s\"\n", pEventAsKey->GetName ()); // event name
	DevMsg ("{\n"); // print the open brace
	// display the whole key/value tree for this event
	for (pKey = pEventAsKey->GetFirstSubKey (); pKey; pKey = pKey->GetNextKey ())
	{
		// given the data type, print out the data
		if (strcmp (pKey->GetString (), "none") == 0)
			DevMsg ("   \"%s\", no value (TYPE_NONE)\n", pKey->GetName ());
		else if (strcmp (pKey->GetString (), "string") == 0)
			DevMsg ("   \"%s\" (TYPE_STRING)\n", pKey->GetName ());
		else if (strcmp (pKey->GetString (), "bool") == 0)
			DevMsg ("   \"%s\" (TYPE_BOOL)\n", pKey->GetName ());
		else if (strcmp (pKey->GetString (), "byte") == 0)
			DevMsg ("   \"%s\" (TYPE_BYTE)\n", pKey->GetName ());
		else if (strcmp (pKey->GetString (), "short") == 0)
			DevMsg ("   \"%s\" (TYPE_SHORT)\n", pKey->GetName ());
		else if (strcmp (pKey->GetString (), "long") == 0)
			DevMsg ("   \"%s\" (TYPE_LONG)\n", pKey->GetName ());
		else if (strcmp (pKey->GetString (), "float") == 0)
			DevMsg ("   \"%s\" (TYPE_FLOAT)\n", pKey->GetName ());
	}
	DevMsg ("}\n"); // print the closing brace
	return;
}

CON_COMMAND( listevt, "List all the events matching a given pattern" )
{
	KeyValues *pEventAsKey;
	int event_count;
	char arg1[128]; // no way, I hate pointers...
	Q_snprintf(arg1, 128, args[1]); // get any argument to the command
	// tell people what we are going to do
	if (arg1[0] == 0)
		DevMsg ("Printing out ALL game, server and MOD events...\n");
	else
		DevMsg ("Printing out game, server and MOD events matching \"%s\"...\n", arg1);
	DevMsg ("Game events:\n");
	event_count = 0;
	if(g_pGameEvents)
	{
		for(pEventAsKey = g_pGameEvents->GetFirstSubKey (); pEventAsKey; pEventAsKey = pEventAsKey->GetNextKey ())
		{
			// if we want a particular pattern AND this event does not match
			if ((arg1[0] != 0) && (strstr (pEventAsKey->GetName (), arg1) == NULL))
				continue; // then skip it
			g_pEventHandler->PrintEventStructure (pEventAsKey); // display this event's structure
			event_count++;
		}
	}	
	DevMsg ("%d game event%s.\n", event_count, (event_count > 1 ? "s" : ""));
	DevMsg ("Engine events:\n");
	event_count = 0;
	if(g_pEngineEvents)
	{
		for(pEventAsKey = g_pEngineEvents->GetFirstSubKey (); pEventAsKey; pEventAsKey = pEventAsKey->GetNextKey ())
		{
			// if we want a particular pattern AND this event does not match
			if ((arg1[0] != 0) && (strstr (pEventAsKey->GetName (), arg1) == NULL))
				continue; // then skip it
			g_pEventHandler->PrintEventStructure (pEventAsKey); // display this event's structure
			event_count++;
		}
	}	
	DevMsg ("%d engine event%s.\n", event_count, (event_count > 1 ? "s" : ""));
	DevMsg ("MOD events:\n");
	event_count = 0;
	if(g_pModEvents)
	{
		for(pEventAsKey = g_pModEvents->GetFirstSubKey (); pEventAsKey; pEventAsKey = pEventAsKey->GetNextKey ())
		{
			// if we want a particular pattern AND this event does not match
			if ((arg1[0] != 0) && (strstr (pEventAsKey->GetName (), arg1) == NULL))
				continue; // then skip it
			g_pEventHandler->PrintEventStructure (pEventAsKey); // display this event's structure
			event_count++;
		}
	}	
	DevMsg ("%d MOD event%s.\n", event_count, (event_count > 1 ? "s" : ""));
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

void event_ServerMessage(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_ServerPlayerConnect(IGameEvent *_event)
{
	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	if(pPlayer)
	{
		omnibot_interface::Notify_ClientConnected( pPlayer, _event->GetBool( "bot" ) );
	}
}

void event_ServerPlayerDisConnect(IGameEvent *_event)
{
	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	if(pPlayer)
	{
		omnibot_interface::Notify_ClientDisConnected( pPlayer );
	}
}

void event_ServerPlayerInfo(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_ServerPlayerActivate(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_ServerPlayerSay(IGameEvent *_event)
{
	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	if(pPlayer)
	{
		const char *pMsg = _event->GetString("text", "");
		if(pMsg)
			omnibot_interface::Notify_ChatMsg( pPlayer, pMsg );
	}
}

void event_ServerPlayerSayTeam(IGameEvent *_event)
{
	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	if(pPlayer)
	{
		const char *pMsg = _event->GetString("text", "");
		if(pMsg)
			omnibot_interface::Notify_TeamChatMsg( pPlayer, pMsg );
	}
}

void event_GameTeamInfo(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_GameTeamScore(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_GamePlayerScore(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

//void event_GamePlayerShoot(IGameEvent *_event)
//{
//	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
//	if(pPlayer)
//	{
//		Notify_PlayerShoot(pPlayer, _event->GetInt("weapon"), 0);
//	}
//}

void event_GamePlayerUse(IGameEvent *_event)
{
	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	CBaseEntity *pUsedEnt = CBaseEntity::Instance(_event->GetInt("entity"));
	if(pPlayer && pUsedEnt)
	{
		omnibot_interface::Notify_PlayerUsed( pPlayer, pUsedEnt );
	}
}

void event_GamePlayerChangeName(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_GameNewMap(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_GameStart(IGameEvent *_event)
{
	omnibot_interface::Notify_GameStarted();
}

void event_GameEnd(IGameEvent *_event)
{
	int iWinner = _event->GetInt("winner");
	omnibot_interface::Notify_GameEnded( obUtilGetBotTeamFromGameTeam( iWinner ) );
}

void event_RoundStart(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_RoundEnd(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_GameMessage(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_BreakBreakable(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_BreakProp(IGameEvent *_event)
{
	DevMsg(__FUNCTION__);
}

void event_Class(IGameEvent *_event)
{
	/*CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	if(pPlayer)
	{
		Notify_ChangedClass(pPlayer, _event->GetInt("oldclass"), _event->GetInt("newclass"));
	}*/
}

void event_Team(IGameEvent *_event)
{
	/*CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	if(pPlayer)
	{
		Notify_ChangedTeam(pPlayer, _event->GetInt("team"));
	}*/
}

void event_Hurt(IGameEvent *_event)
{
	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	CBasePlayer *pAttacker = UTIL_PlayerByUserId(_event->GetInt("attacker")); // FIXME
	if(pPlayer)
	{
		omnibot_interface::Notify_Hurt( pPlayer, pAttacker ? pAttacker : 0 );
	}	
}

void event_Death(IGameEvent *_event)
{
	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
	CBasePlayer *pAttacker = UTIL_PlayerByUserId(_event->GetInt("attacker")); // FIXME
	const char *pWeapon = _event->GetString("weapon");
	if(pPlayer)
	{
		omnibot_interface::Notify_Death( pPlayer, pAttacker ? pAttacker : 0, pWeapon );
	}
	if(pAttacker)
	{
		omnibot_interface::Notify_KilledSomeone( pAttacker, pPlayer ? pPlayer : 0, pWeapon );
	}
}

//void event_Spawn(IGameEvent *_event)
//{
//	CBasePlayer *pPlayer = UTIL_PlayerByUserId(_event->GetInt("userid"));
//	if(pPlayer)
//	{
//		Notify_Spawned(pPlayer);
//	}
//}

