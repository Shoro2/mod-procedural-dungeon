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
#include "PDInstanceScript.h"
#include "PDMgr.h"
#include "ScriptMgr.h"

// Attaches the procedural InstanceScript to every fresh InstanceMap of the
// configured base map. Runs before the instance_template.ScriptId path, so no
// core or base-DB script binding is required.
class PDMapScript : public AllMapScript
{
public:
    PDMapScript() : AllMapScript("PDMapScript", { ALLMAPHOOK_ON_BEFORE_CREATE_INSTANCE_SCRIPT }) { }

    void OnBeforeCreateInstanceScript(InstanceMap* instanceMap, InstanceScript** instanceData, bool load, std::string /*data*/, uint32 /*completedEncounterMask*/) override
    {
        if (!instanceMap || !instanceData || *instanceData)
        {
            return;
        }
        if (!sPDMgr->IsEnabled() || instanceMap->GetId() != sPDMgr->GetConfig().baseMapId)
        {
            return;
        }

        *instanceData = new PDungeon::PDInstanceScript(instanceMap);
        LOG_DEBUG(PDungeon::PD_LOG, "attached PDInstanceScript to map {} instance {} (load: {})",
                  instanceMap->GetId(), instanceMap->GetInstanceId(), load);
    }
};

void AddPDMapScripts()
{
    new PDMapScript();
}
