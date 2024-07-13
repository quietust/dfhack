#include <stdint.h>
#include <iostream>
#include <map>
#include <vector>
#include "Core.h"
#include "Console.h"
#include "Export.h"
#include "PluginManager.h"
#include "TileTypes.h"
#include "modules/Maps.h"
#include "modules/World.h"
#include "df/block_square_event_frozen_liquidst.h"

using std::string;
using std::vector;

using namespace DFHack;
using namespace df::enums;

DFHACK_PLUGIN("freshwater");

REQUIRE_GLOBAL(world);

command_result freshwater(color_ostream &out, vector<string> & params)
{
    CoreSuspender suspend;

    if (!Maps::IsValid())
    {
        out.printerr("Map is not available!\n");
        return CR_FAILURE;
    }

    int freshened = 0, stagnated = 0;
    for (size_t i = 0; i < world->map.map_blocks.size(); i++)
    {
        df::map_block *block = world->map.map_blocks[i];
        // for each tile in block
        for (uint32_t x = 0; x < 16; x++) for (uint32_t y = 0; y < 16; y++)
        {
            df::tiletype tile = block->tiletype[x][y];
            // If the tile is ice, check what it is underneath
            if (tileMaterial(tile) == tiletype_material::FROZEN_LIQUID)
            {
                for (size_t j = 0; j < block->block_events.size(); j++)
                {
                    auto bse = block->block_events[j];
                    if (bse->getType() != block_square_event_type::frozen_liquid)
                        continue;
                    auto fl = (df::block_square_event_frozen_liquidst *)bse;
                    if (fl->tiles[x][y] != tiletype::Void)
                    {
                        tile = fl->tiles[x][y];
                        break;
                    }
                }
            }
            bool murky = (tile == tiletype::MurkyPool);
            if (block->designation[x][y].bits.water_stagnant != murky)
            {
                block->designation[x][y].bits.water_stagnant = murky;
                (murky ? stagnated : freshened)++;
            }
        }
    }
    out.print("%i tiles freshened, %i tiles stagnated.\n", freshened, stagnated);
    return CR_OK;
}

DFhackCExport command_result plugin_init ( color_ostream &out, vector <PluginCommand> &commands)
{
    commands.push_back(PluginCommand("freshwater","Freshen water sources.",freshwater,false,
        "Marks all map tiles as non-stagnant, then reflags murky pools as stagnant.\n"
    ));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( color_ostream &out )
{
    return CR_OK;
}
