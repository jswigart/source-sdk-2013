#ifndef __OMNIBOT_EVENTHANDLER_H__
#define __OMNIBOT_EVENTHANDLER_H__

#include "GameEventListener.h"

class CBaseEntity;
class CBasePlayer;

extern KeyValues *g_pGameEvents;
extern KeyValues *g_pEngineEvents;
extern KeyValues *g_pModEvents;

class omnibot_eventhandler : public CGameEventListener
{
public:

	void RegisterEvents();
	void UnRegisterEvents();

	void ExtractEvents();
	void PrintEvent(IGameEvent *pEvent);
	void PrintEventStructure(KeyValues *pEventAsKey);
	void FireGameEvent(IGameEvent *_event);
};

extern omnibot_eventhandler *g_pEventHandler;

#endif
