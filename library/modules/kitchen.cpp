
#include "Internal.h"

#include <string>
#include <sstream>
#include <vector>
#include <cstdio>
#include <map>
#include <set>
using namespace std;

#include "VersionInfo.h"
#include "MemAccess.h"
#include "Types.h"
#include "Error.h"
#include "modules/kitchen.h"
#include "ModuleFactory.h"
#include "Core.h"
using namespace DFHack;

#include "DataDefs.h"
#include "df/world.h"
#include "df/plotinfost.h"
#include "df/item_type.h"
#include "df/plant_raw.h"
#include "df/plant_material_def.h"

using namespace df::enums;
using df::global::world;
using df::global::plotinfo;

void Kitchen::debug_print(color_ostream &out)
{
    out.print("Kitchen Exclusions\n");
    for(std::size_t i = 0; i < size(); ++i)
    {
        out.print("%2u: IT:%2i IS:%i MT:%3i MI:%2i ET:%i %s\n",
                       i,
                       plotinfo->kitchen.item_types[i],
                       plotinfo->kitchen.item_subtypes[i],
                       plotinfo->kitchen.mat_types[i],
                       plotinfo->kitchen.mat_indices[i],
                       plotinfo->kitchen.exc_types[i].whole,
                       (plotinfo->kitchen.mat_types[i] >= 419 && plotinfo->kitchen.mat_types[i] <= 618) ? world->raws.plants.all[plotinfo->kitchen.mat_indices[i]]->id.c_str() : "n/a"
        );
    }
    out.print("\n");
}

void Kitchen::allowPlantSeedCookery(t_materialIndex materialIndex)
{
    bool match = false;
    do
    {
        match = false;
        std::size_t matchIndex = 0;
        for(std::size_t i = 0; i < size(); ++i)
        {
            if(plotinfo->kitchen.mat_indices[i] == materialIndex
               && (plotinfo->kitchen.item_types[i] == item_type::SEEDS || plotinfo->kitchen.item_types[i] == item_type::PLANT)
               && plotinfo->kitchen.exc_types[i].whole == cookingExclusion
            )
            {
                match = true;
                matchIndex = i;
            }
        }
        if(match)
        {
            plotinfo->kitchen.item_types.erase(plotinfo->kitchen.item_types.begin() + matchIndex);
            plotinfo->kitchen.item_subtypes.erase(plotinfo->kitchen.item_subtypes.begin() + matchIndex);
            plotinfo->kitchen.mat_indices.erase(plotinfo->kitchen.mat_indices.begin() + matchIndex);
            plotinfo->kitchen.mat_types.erase(plotinfo->kitchen.mat_types.begin() + matchIndex);
            plotinfo->kitchen.exc_types.erase(plotinfo->kitchen.exc_types.begin() + matchIndex);
        }
    } while(match);
};

void Kitchen::denyPlantSeedCookery(t_materialIndex materialIndex)
{
    df::plant_raw *type = world->raws.plants.all[materialIndex];
    bool SeedAlreadyIn = false;
    bool PlantAlreadyIn = false;
    for(std::size_t i = 0; i < size(); ++i)
    {
        if(plotinfo->kitchen.mat_indices[i] == materialIndex
           && plotinfo->kitchen.exc_types[i].whole == cookingExclusion)
        {
            if(plotinfo->kitchen.item_types[i] == item_type::SEEDS)
                SeedAlreadyIn = true;
            else if (plotinfo->kitchen.item_types[i] == item_type::PLANT)
                PlantAlreadyIn = true;
        }
    }
    if(!SeedAlreadyIn)
    {
        plotinfo->kitchen.item_types.push_back(item_type::SEEDS);
        plotinfo->kitchen.item_subtypes.push_back(organicSubtype);
        plotinfo->kitchen.mat_types.push_back(type->material_defs.type[plant_material_def::seed]);
        plotinfo->kitchen.mat_indices.push_back(materialIndex);
        plotinfo->kitchen.exc_types.push_back(cookingExclusion);
    }
    if(!PlantAlreadyIn)
    {
        plotinfo->kitchen.item_types.push_back(item_type::PLANT);
        plotinfo->kitchen.item_subtypes.push_back(organicSubtype);
        plotinfo->kitchen.mat_types.push_back(type->material_defs.type[plant_material_def::basic_mat]);
        plotinfo->kitchen.mat_indices.push_back(materialIndex);
        plotinfo->kitchen.exc_types.push_back(cookingExclusion);
    }
};

void Kitchen::fillWatchMap(std::map<t_materialIndex, unsigned int>& watchMap)
{
    watchMap.clear();
    for(std::size_t i = 0; i < size(); ++i)
    {
        if(plotinfo->kitchen.item_subtypes[i] == (short)limitType && plotinfo->kitchen.item_subtypes[i] == (short)limitSubtype && plotinfo->kitchen.exc_types[i].whole == limitExclusion)
        {
            watchMap[plotinfo->kitchen.mat_indices[i]] = (unsigned int) plotinfo->kitchen.mat_types[i];
        }
    }
};

void Kitchen::removeLimit(t_materialIndex materialIndex)
{
    bool match = false;
    do
    {
        match = false;
        std::size_t matchIndex = 0;
        for(std::size_t i = 0; i < size(); ++i)
        {
            if(plotinfo->kitchen.item_types[i] == limitType
               && plotinfo->kitchen.item_subtypes[i] == limitSubtype
               && plotinfo->kitchen.mat_indices[i] == materialIndex
               && plotinfo->kitchen.exc_types[i].whole == limitExclusion)
            {
                match = true;
                matchIndex = i;
            }
        }
        if(match)
        {
            plotinfo->kitchen.item_types.erase(plotinfo->kitchen.item_types.begin() + matchIndex);
            plotinfo->kitchen.item_subtypes.erase(plotinfo->kitchen.item_subtypes.begin() + matchIndex);
            plotinfo->kitchen.mat_types.erase(plotinfo->kitchen.mat_types.begin() + matchIndex);
            plotinfo->kitchen.mat_indices.erase(plotinfo->kitchen.mat_indices.begin() + matchIndex);
            plotinfo->kitchen.exc_types.erase(plotinfo->kitchen.exc_types.begin() + matchIndex);
        }
    } while(match);
};

void Kitchen::setLimit(t_materialIndex materialIndex, unsigned int limit)
{
    removeLimit(materialIndex);
    if(limit > seedLimit)
    {
        limit = seedLimit;
    }
    plotinfo->kitchen.item_types.push_back(limitType);
    plotinfo->kitchen.item_subtypes.push_back(limitSubtype);
    plotinfo->kitchen.mat_indices.push_back(materialIndex);
    plotinfo->kitchen.mat_types.push_back((t_materialType) (limit < seedLimit) ? limit : seedLimit);
    plotinfo->kitchen.exc_types.push_back(limitExclusion);
};

void Kitchen::clearLimits()
{
    bool match = false;
    do
    {
        match = false;
        std::size_t matchIndex;
        for(std::size_t i = 0; i < size(); ++i)
        {
            if(plotinfo->kitchen.item_types[i] == limitType
               && plotinfo->kitchen.item_subtypes[i] == limitSubtype
               && plotinfo->kitchen.exc_types[i].whole == limitExclusion)
            {
                match = true;
                matchIndex = i;
            }
        }
        if(match)
        {
            plotinfo->kitchen.item_types.erase(plotinfo->kitchen.item_types.begin() + matchIndex);
            plotinfo->kitchen.item_subtypes.erase(plotinfo->kitchen.item_subtypes.begin() + matchIndex);
            plotinfo->kitchen.mat_indices.erase(plotinfo->kitchen.mat_indices.begin() + matchIndex);
            plotinfo->kitchen.mat_types.erase(plotinfo->kitchen.mat_types.begin() + matchIndex);
            plotinfo->kitchen.exc_types.erase(plotinfo->kitchen.exc_types.begin() + matchIndex);
        }
    } while(match);
};

size_t Kitchen::size()
{
    return plotinfo->kitchen.item_types.size();
};
