/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Log.h"
#include "Map.h"
#include "PDDefines.h"
#include "PDv2InstanceScript.h"
#include "PDv2Mgr.h"
#include "ScriptMgr.h"

// Attaches the PDv2 instance script to every fresh InstanceMap of the v2 map.
//
// Separate from v1's PDMapScript rather than folded into it: the two watch
// different map ids and must not race to attach to the same instance. If both
// were ever pointed at the same map, whichever ran first would win and the other
// would silently do nothing - so they are kept apart and the guard below is
// explicit about which map this one owns.
class PDv2MapScript : public AllMapScript
{
public:
    PDv2MapScript() : AllMapScript("PDv2MapScript", { ALLMAPHOOK_ON_BEFORE_CREATE_INSTANCE_SCRIPT }) { }

    void OnBeforeCreateInstanceScript(InstanceMap* instanceMap, InstanceScript** instanceData,
                                      bool load, std::string /*data*/,
                                      uint32 /*completedEncounterMask*/) override
    {
        if (!instanceMap || !instanceData || *instanceData)
        {
            return;
        }
        if (!sPDv2Mgr->IsEnabled() || instanceMap->GetId() != sPDv2Mgr->GetConfig().mapId)
        {
            return;
        }

        *instanceData = new PDungeon::PDv2InstanceScript(instanceMap);
        LOG_DEBUG(PDungeon::PD_LOG, "PDv2: attached instance script to map {} instance {} (load {})",
                  instanceMap->GetId(), instanceMap->GetInstanceId(), load);
    }
};

void AddPDv2MapScripts()
{
    new PDv2MapScript();
}
