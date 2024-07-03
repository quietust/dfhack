// All above-ground soil not covered by buildings will be covered with grass.
// Necessary for worlds generated prior to version 0.31.19 - otherwise, outdoor shrubs and trees no longer grow.

#include "Core.h"
#include "Console.h"
#include "Export.h"
#include "PluginManager.h"

#include "DataDefs.h"
#include "df/world.h"
#include "df/world_raws.h"
#include "df/plant_raw.h"

#include "df/map_block.h"
#include "df/block_square_event.h"
//#include "df/block_square_event_type.h"
#include "df/block_square_event_grassst.h"
#include "TileTypes.h"

using std::string;
using std::vector;
using namespace std;
using namespace DFHack;

DFHACK_PLUGIN("regrass");
REQUIRE_GLOBAL(world);

command_result df_regrass (color_ostream &out, vector <string> & parameters);

DFhackCExport command_result plugin_init (color_ostream &out, std::vector<PluginCommand> &commands)
{
    commands.push_back(PluginCommand("regrass", "Regrows surface grass.", df_regrass, false,
        "Specify parameter 'max' to set all grass types to full density, otherwise only one type of grass will be restored per tile.\n"));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( color_ostream &out )
{
    return CR_OK;
}

command_result df_regrass (color_ostream &out, vector <string> & parameters)
{
    bool max = false;
    if (!parameters.empty())
    {
        if(parameters[0] == "max")
            max = true;
        else
            return CR_WRONG_USAGE;
    }

    CoreSuspender suspend;

    int count = 0;
    for (size_t i = 0; i < world->map.map_blocks.size(); i++)
    {
        df::map_block *cur = world->map.map_blocks[i];

        // check block for grass events before looking at 16x16 tiles
        df::block_square_event_grassst * grev = NULL;
        for(size_t e=0; e<cur->block_events.size(); e++)
        {
            df::block_square_event * blev = cur->block_events[e];
            df::block_square_event_type blevtype = blev->getType();
            if(blevtype == df::block_square_event_type::grass)
            {
                grev = (df::block_square_event_grassst *)blev;
                break;
            }
        }
        if(!grev && !world->raws.plants.grasses.size())
        {
            // If we didn't find any grass events, then at least create "fake" grass
            // for old fortresses which predated the introduction of custom grass types
            // so that new saplings and shrubs are capable of growing on the surface
            for (int y = 0; y < 16; y++)
            {
                for (int x = 0; x < 16; x++)
                {
                    if (   tileShape(cur->tiletype[x][y]) != tiletype_shape::FLOOR
                        || cur->designation[x][y].bits.subterranean
                        || cur->occupancy[x][y].bits.building
                        || cur->occupancy[x][y].bits.no_grow)
                        continue;

                    // don't touch furrowed tiles (dirt roads made on soil)
                    if(tileSpecial(cur->tiletype[x][y]) == tiletype_special::FURROWED)
                        continue;

                    int mat = tileMaterial(cur->tiletype[x][y]);
                    if (   mat != tiletype_material::SOIL
                        && mat != tiletype_material::ASHES) // refill ashes too
                        continue;

                    cur->tiletype[x][y] = findRandomVariant((rand() & 1) ? tiletype::GrassLightFloor1 : tiletype::GrassDarkFloor1);
                    count++;
                }
            }
            continue;
        }

        for (int y = 0; y < 16; y++)
        {
            for (int x = 0; x < 16; x++)
            {
                // don't touch walls, tiles under buildings, or ones where grass isn't supposed to grow
                if (   tileShape(cur->tiletype[x][y]) == tiletype_shape::WALL || cur->occupancy[x][y].bits.building || cur->occupancy[x][y].bits.no_grow)
                    continue;

                // don't touch furrowed tiles (dirt roads made on soil)
                if(tileSpecial(cur->tiletype[x][y]) == tiletype_special::FURROWED)
                    continue;

                auto mat = tileMaterial(cur->tiletype[x][y]);
                if (   mat != tiletype_material::SOIL
                    && mat != tiletype_material::GRASS_DARK  // refill existing grass, too
                    && mat != tiletype_material::GRASS_LIGHT
                    && mat != tiletype_material::GRASS_DRY
                    && mat != tiletype_material::GRASS_DEAD
                    )
                    continue;

                // max = set amounts of all grass events on that tile to 100
                bool regrew = false;
                if(max)
                {
                    for(size_t e=0; e<cur->block_events.size(); e++)
                    {
                        df::block_square_event * blev = cur->block_events[e];
                        df::block_square_event_type blevtype = blev->getType();
                        if(blevtype == df::block_square_event_type::grass)
                        {
                            df::block_square_event_grassst * gr_ev = (df::block_square_event_grassst *)blev;
                            gr_ev->amount[x][y] = 100;
                            regrew = true;
                        }
                    }
                }
                else
                {
                    // try to find the 'original' event
                    for(size_t e=0; e<cur->block_events.size(); e++)
                    {
                        df::block_square_event * blev = cur->block_events[e];
                        df::block_square_event_type blevtype = blev->getType();
                        if(blevtype == df::block_square_event_type::grass)
                        {
                            df::block_square_event_grassst * gr_ev = (df::block_square_event_grassst *)blev;
                            if(gr_ev->amount[x][y] > 0)
                            {
                                gr_ev->amount[x][y] = 100;
                                regrew = true;
                                break;
                            }
                        }
                    }
                    // if original could not be found (meaning it was already depleted):
                    // refresh random grass event in the map block
                    if(!regrew)
                    {
                        vector <df::block_square_event_grassst *> gr_evs;
                        for(size_t e=0; e<cur->block_events.size(); e++)
                        {
                            df::block_square_event * blev = cur->block_events[e];
                            df::block_square_event_type blevtype = blev->getType();
                            if(blevtype == df::block_square_event_type::grass)
                            {
                                df::block_square_event_grassst * gr_ev = (df::block_square_event_grassst *)blev;
                                gr_evs.push_back(gr_ev);
                            }
                        }
                        if (gr_evs.size() > 0)
                        {
                            int r = rand() % gr_evs.size();
                            gr_evs[r]->amount[x][y]=100;
                            regrew = true;
                        }
                    }
                }
                // If it was soil and we regrew it, turn it into grass
                if (regrew && mat == tiletype_material::SOIL)
                    cur->tiletype[x][y] = DFHack::findRandomVariant(DFHack::findTileType(tileShape(cur->tiletype[x][y]),
                        (rand() & 1) ? tiletype_material::GRASS_LIGHT : tiletype_material::GRASS_DARK,
                        tiletype_variant::NONE, tiletype_special::NORMAL, DFHack::TileDirection()));
                // If it was grass and we didn't regrow it (because there was no grass event), turn it into soil so it can regrow naturally
                else if (!regrew && mat != tiletype_material::SOIL)
                    cur->tiletype[x][y] = DFHack::findRandomVariant(DFHack::findTileType(tileShape(cur->tiletype[x][y]),
                        tiletype_material::SOIL, tiletype_variant::NONE, tiletype_special::NORMAL, DFHack::TileDirection()));
                count++;
            }
        }
    }

    if (count)
        out.print("Regrew %d tiles of grass.\n", count);
    return CR_OK;
}
