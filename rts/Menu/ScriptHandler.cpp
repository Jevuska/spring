/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "ScriptHandler.h"

#include "ExternalAI/Interface/aidefines.h"
#include "ExternalAI/IAILibraryManager.h"
#include "ExternalAI/SkirmishAIKey.h"

#include <stdexcept>

CScriptHandler& CScriptHandler::Instance()
{
	static CScriptHandler instance;
	return instance;
}


bool CScriptHandler::IsSkirmishAITestScript(const std::string& scriptName) const
{
	const ScriptMap::const_iterator scriptsIt = scripts.find(scriptName);

	if (scriptsIt == scripts.end())
		return false;

	return (dynamic_cast<const CSkirmishAIScript*>(scriptsIt->second) != NULL);
}


const SkirmishAIData& CScriptHandler::GetSkirmishAIData(const std::string& scriptName) const
{
	const ScriptMap::const_iterator scriptsIt = scripts.find(scriptName);

	if (scriptsIt == scripts.end()) {
		throw std::runtime_error("start-script \"" + scriptName + "\" does not exist");
	}

	const CSkirmishAIScript* aiScript = dynamic_cast<const CSkirmishAIScript*>(scriptsIt->second);

	if (aiScript == NULL) {
		throw std::runtime_error("start-script \"" + scriptName + "\" is not a CSkirmishAIScript");
	}

	return aiScript->aiData;
}


void CScriptHandler::Add(CScript* script)
{
	scripts.insert(ScriptMap::value_type(script->name, script));
	scriptNames.push_back(script->name);
}


CScriptHandler::CScriptHandler()
{
	// default script
	Add(new CScript("Player Only: Testing Sandbox"));

	// add the C interface Skirmish AIs
	// Lua AIs can not be added, as the selection would get invalid when
	// selecting another mod.
	const IAILibraryManager::T_skirmishAIKeys& skirmishAIKeys = aiLibManager->GetSkirmishAIKeys();

	IAILibraryManager::T_skirmishAIKeys::const_iterator i = skirmishAIKeys.begin();
	IAILibraryManager::T_skirmishAIKeys::const_iterator e = skirmishAIKeys.end();

	for (; i != e; ++i) {
		SkirmishAIData aiData;
		aiData.shortName = i->GetShortName();
		aiData.version   = i->GetVersion();
		aiData.isLuaAI   = false;

		Add(new CSkirmishAIScript(aiData));
	}
}


CScriptHandler::~CScriptHandler()
{
	for (ScriptMap::iterator it = scripts.begin(); it != scripts.end(); ++it) {
		delete it->second;
	}
}
