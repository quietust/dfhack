// some headers required for a plugin. Nothing special, just the basics.
#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

// DF data structure definition headers
#include "DataDefs.h"
#include "df/world.h"
#include "df/plotinfost.h"
#include "df/item.h"
#include "df/creature_raw.h"
#include "df/caste_raw.h"
#include "df/body_part_raw.h"
#include "df/historical_figure.h"

#include "df/job_item.h"
#include "df/job_material_category.h"
#include "df/dfhack_material_category.h"
#include "df/matter_state.h"
#include "df/material_vec_ref.h"
#include "df/builtin_mats.h"
#include "df/map_block_column.h"
#include "df/plant.h"
#include "df/plant_raw_flags.h"
#include "df/itemdef.h"
#include "df/building_def_workshopst.h"
#include "df/building_def_furnacest.h"
#include "df/building_wellst.h"
#include "df/building_water_wheelst.h"
#include "df/building_screw_pumpst.h"
#include "df/building_axle_horizontalst.h"
#include "df/building_windmillst.h"
#include "df/building_siegeenginest.h"
#include "df/building_bridgest.h"

#include "df/descriptor_color.h"
#include "df/descriptor_pattern.h"
#include "df/descriptor_shape.h"

#include "df/physical_attribute_type.h"
#include "df/mental_attribute_type.h"
#include "df/color_modifier_raw.h"
#include "df/descriptor_color.h"
#include "df/descriptor_pattern.h"

#include "df/region_map_entry.h"
#include "df/world_region_details.h"
#include "df/world_region.h"

#include "df/unit.h"
#include "df/creature_raw.h"
#include "df/caste_raw.h"
#include "df/tissue.h"

#include "df/enabler.h"
#include "df/graphicst.h"

#include "df/viewscreen_choose_start_sitest.h"

#include "df/bp_appearance_modifier.h"
#include "df/body_part_layer_raw.h"
#include "df/body_appearance_modifier.h"

//DFhack specific headers
#include "modules/Maps.h"
#include "modules/MapCache.h"
#include "modules/Materials.h"
#include "modules/Gui.h"
#include "modules/Translation.h"
#include "modules/Items.h"
#include "modules/Buildings.h"
#include "modules/Units.h"
#include "modules/World.h"
#include "TileTypes.h"
#include "MiscUtils.h"
#include "Hooks.h"
#include "SDL_events.h"
#include "SDL_keyboard.h"

#include <vector>
#include <time.h>
#include <cstdio>

#include "RemoteFortressReader.pb.h"

#include "RemoteServer.h"

using namespace DFHack;
using namespace df::enums;
using namespace RemoteFortressReader;
using namespace std;

DFHACK_PLUGIN("RemoteFortressReader");
using namespace df::global;
REQUIRE_GLOBAL(world);
REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(gamemode);

// Here go all the command declarations...
// mostly to allow having the mandatory stuff on top of the file and commands on the bottom

static command_result GetGrowthList(color_ostream &stream, const EmptyMessage *in, MaterialList *out);
static command_result GetMaterialList(color_ostream &stream, const EmptyMessage *in, MaterialList *out);
static command_result GetTiletypeList(color_ostream &stream, const EmptyMessage *in, TiletypeList *out);
static command_result GetBlockList(color_ostream &stream, const BlockRequest *in, BlockList *out);
static command_result GetPlantList(color_ostream &stream, const BlockRequest *in, PlantList *out);
static command_result CheckHashes(color_ostream &stream, const EmptyMessage *in);
static command_result GetUnitList(color_ostream &stream, const EmptyMessage *in, UnitList *out);
static command_result GetViewInfo(color_ostream &stream, const EmptyMessage *in, ViewInfo *out);
static command_result GetMapInfo(color_ostream &stream, const EmptyMessage *in, MapInfo *out);
static command_result ResetMapHashes(color_ostream &stream, const EmptyMessage *in);
static command_result GetItemList(color_ostream &stream, const EmptyMessage *in, MaterialList *out);
static command_result GetBuildingDefList(color_ostream &stream, const EmptyMessage *in, BuildingList *out);
static command_result GetWorldMap(color_ostream &stream, const EmptyMessage *in, WorldMap *out);
static command_result GetWorldMapCenter(color_ostream &stream, const EmptyMessage *in, WorldMap *out);
static command_result GetRegionMaps(color_ostream &stream, const EmptyMessage *in, RegionMaps *out);
static command_result GetCreatureRaws(color_ostream &stream, const EmptyMessage *in, CreatureRawList *out);
static command_result GetPlantRaws(color_ostream &stream, const EmptyMessage *in, PlantRawList *out);
static command_result CopyScreen(color_ostream &stream, const EmptyMessage *in, ScreenCapture *out);
static command_result PassKeyboardEvent(color_ostream &stream, const KeyboardEvent *in);


void CopyBlock(df::map_block * DfBlock, RemoteFortressReader::MapBlock * NetBlock, MapExtras::MapCache * MC, DFCoord pos);
void FindChangedBlocks();

command_result dump_bp_mods(color_ostream &out, vector <string> & parameters)
{
    remove("bp_appearance_mods.csv");
    ofstream output;
    output.open("bp_appearance_mods.csv");

    output << "Race Index;Race;Caste;Bodypart Token;Bodypart Name;Tissue Layer;Modifier Type;Range\n";

    for (int creatureIndex = 0; creatureIndex < world->raws.creatures.all.size(); creatureIndex++)
    {
        auto creatureRaw = world->raws.creatures.all[creatureIndex];
        for (int casteIndex = 0; casteIndex < creatureRaw->caste.size(); casteIndex++)
        {
            df::caste_raw *casteRaw = creatureRaw->caste[casteIndex];
            for (int partIndex = 0; partIndex < casteRaw->bp_appearance.part_idx.size(); partIndex++)
            {
                output << creatureIndex << ";";
                output << creatureRaw->creature_id << ";";
                output << casteRaw->caste_id << ";";
                output << casteRaw->body_info.body_parts[casteRaw->bp_appearance.part_idx[partIndex]]->token << ";";
                output << casteRaw->body_info.body_parts[casteRaw->bp_appearance.part_idx[partIndex]]->name_singular[0]->c_str() << ";";
                int layer = casteRaw->bp_appearance.layer_idx[partIndex];
                if (layer < 0)
                    output << "N/A;";
                else
                    output << casteRaw->body_info.body_parts[casteRaw->bp_appearance.part_idx[partIndex]]->layers[layer]->layer_name << ";";
                output << ENUM_KEY_STR(appearance_modifier_type, casteRaw->bp_appearance.modifiers[casteRaw->bp_appearance.modifier_idx[partIndex]]->modifier.type) << ";";
                auto appMod = casteRaw->bp_appearance.modifiers[casteRaw->bp_appearance.modifier_idx[partIndex]];
                if (appMod->modifier.growth_rate > 0)
                {
                    output << appMod->modifier.growth_min << " - " << appMod->modifier.growth_max << "\n";
                }
                else
                {
                    output << casteRaw->bp_appearance.modifiers[casteRaw->bp_appearance.modifier_idx[partIndex]]->modifier.ranges[0] << " - ";
                    output << casteRaw->bp_appearance.modifiers[casteRaw->bp_appearance.modifier_idx[partIndex]]->modifier.ranges[6] << "\n";
                }
            }
        }
    }

    output.close();

    return CR_OK;
}


// Mandatory init function. If you have some global state, create it here.
DFhackCExport command_result plugin_init(color_ostream &out, std::vector <PluginCommand> &commands)
{
    //// Fill the command list with your commands.
    commands.push_back(PluginCommand(
        "dump_bp_mods", "Dump bodypart mods for debugging",
        dump_bp_mods, false, /* true means that the command can't be used from non-interactive user interface */
        // Extended help string. Used by CR_WRONG_USAGE and the help command:
        "  This command does nothing at all.\n"
        "Example:\n"
        "  isoworldremote\n"
        "    Does nothing.\n"
    ));
    return CR_OK;
}

DFhackCExport RPCService *plugin_rpcconnect(color_ostream &)
{
    RPCService *svc = new RPCService();
    svc->addFunction("GetMaterialList", GetMaterialList);
    svc->addFunction("GetGrowthList", GetGrowthList);
    svc->addFunction("GetBlockList", GetBlockList);
    svc->addFunction("CheckHashes", CheckHashes);
    svc->addFunction("GetTiletypeList", GetTiletypeList);
    svc->addFunction("GetPlantList", GetPlantList);
    svc->addFunction("GetUnitList", GetUnitList);
    svc->addFunction("GetViewInfo", GetViewInfo);
    svc->addFunction("GetMapInfo", GetMapInfo);
    svc->addFunction("ResetMapHashes", ResetMapHashes);
    svc->addFunction("GetItemList", GetItemList);
    svc->addFunction("GetBuildingDefList", GetBuildingDefList);
    svc->addFunction("GetWorldMap", GetWorldMap);
    svc->addFunction("GetRegionMaps", GetRegionMaps);
    svc->addFunction("GetRegionMapsNew", GetRegionMaps);
    svc->addFunction("GetCreatureRaws", GetCreatureRaws);
    svc->addFunction("GetWorldMapCenter", GetWorldMapCenter);
    svc->addFunction("GetPlantRaws", GetPlantRaws);
    svc->addFunction("CopyScreen", CopyScreen);
    svc->addFunction("PassKeyboardEvent", PassKeyboardEvent);
    return svc;
}

// This is called right before the plugin library is removed from memory.
DFhackCExport command_result plugin_shutdown(color_ostream &out)
{
    // You *MUST* kill all threads you created before this returns.
    // If everything fails, just return CR_FAILURE. Your plugin will be
    // in a zombie state, but things won't crash.
    return CR_OK;
}

uint16_t fletcher16(uint8_t const *data, size_t bytes)
{
    uint16_t sum1 = 0xff, sum2 = 0xff;

    while (bytes) {
        size_t tlen = bytes > 20 ? 20 : bytes;
        bytes -= tlen;
        do {
            sum2 += sum1 += *data++;
        } while (--tlen);
        sum1 = (sum1 & 0xff) + (sum1 >> 8);
        sum2 = (sum2 & 0xff) + (sum2 >> 8);
    }
    /* Second reduction step to reduce sums to 8 bits */
    sum1 = (sum1 & 0xff) + (sum1 >> 8);
    sum2 = (sum2 & 0xff) + (sum2 >> 8);
    return sum2 << 8 | sum1;
}

void ConvertDfColor(int16_t index, RemoteFortressReader::ColorDefinition * out)
{
    if (!df::global::enabler)
        return;

    auto enabler = df::global::enabler;

    out->set_red((int)(enabler->ccolor[index][0] * 255));
    out->set_green((int)(enabler->ccolor[index][1] * 255));
    out->set_blue((int)(enabler->ccolor[index][2] * 255));
}

void ConvertDfColor(int16_t in[3], RemoteFortressReader::ColorDefinition * out)
{
    int index = in[0] | (8 * in[2]);
    ConvertDfColor(index, out);
}

void CopyBuilding(int buildingIndex, RemoteFortressReader::BuildingInstance * remote_build)
{
    df::building * local_build = df::global::world->buildings.all[buildingIndex];
    remote_build->set_index(buildingIndex);
    int minZ = local_build->z;
    if (local_build->getType() == df::enums::building_type::Well)
    {
        df::building_wellst * well_building = virtual_cast<df::building_wellst>(local_build);
        if (well_building)
            minZ = well_building->bucket_z;
    }
    remote_build->set_pos_x_min(local_build->x1);
    remote_build->set_pos_y_min(local_build->y1);
    remote_build->set_pos_z_min(minZ);

    remote_build->set_pos_x_max(local_build->x2);
    remote_build->set_pos_y_max(local_build->y2);
    remote_build->set_pos_z_max(local_build->z);

    auto buildingType = remote_build->mutable_building_type();
    auto type = local_build->getType();
    buildingType->set_building_type(type);
    buildingType->set_building_subtype(local_build->getSubtype());
    buildingType->set_building_custom(local_build->getCustomType());

    auto material = remote_build->mutable_material();
    material->set_mat_type(local_build->mat_type);
    material->set_mat_index(local_build->mat_index);

    remote_build->set_building_flags(local_build->flags.whole);
    remote_build->set_is_room(local_build->is_room);
    if (local_build->is_room || local_build->getType() == df::enums::building_type::Civzone || local_build->getType() == df::enums::building_type::Stockpile)
    {
        auto room = remote_build->mutable_room();
        room->set_pos_x(local_build->room.x);
        room->set_pos_y(local_build->room.y);
        room->set_width(local_build->room.width);
        room->set_height(local_build->room.height);
        for (int i = 0; i < (local_build->room.width * local_build->room.height); i++)
        {
            room->add_extents(local_build->room.extents[i]);
        }
    }


    switch (type)
    {
    case df::enums::building_type::NONE:
        break;
    case df::enums::building_type::Chair:
        break;
    case df::enums::building_type::Bed:
        break;
    case df::enums::building_type::Table:
        break;
    case df::enums::building_type::Coffin:
        break;
    case df::enums::building_type::FarmPlot:
        break;
    case df::enums::building_type::Furnace:
        break;
    case df::enums::building_type::TradeDepot:
        break;
    case df::enums::building_type::Shop:
        break;
    case df::enums::building_type::Door:
        break;
    case df::enums::building_type::Floodgate:
        break;
    case df::enums::building_type::Box:
        break;
    case df::enums::building_type::Weaponrack:
        break;
    case df::enums::building_type::Armorstand:
        break;
    case df::enums::building_type::Workshop:
        break;
    case df::enums::building_type::Cabinet:
        break;
    case df::enums::building_type::Statue:
        break;
    case df::enums::building_type::WindowGlass:
        break;
    case df::enums::building_type::WindowGem:
        break;
    case df::enums::building_type::Well:
        break;
    case df::enums::building_type::Bridge:
    {
        auto actual = strict_virtual_cast<df::building_bridgest>(local_build);
        if (actual)
        {
            auto direction = actual->direction;
            switch (direction)
            {
            case df::building_bridgest::Retracting:
                break;
            case df::building_bridgest::Left:
                remote_build->set_direction(WEST);
                break;
            case df::building_bridgest::Right:
                remote_build->set_direction(EAST);
                break;
            case df::building_bridgest::Up:
                remote_build->set_direction(NORTH);
                break;
            case df::building_bridgest::Down:
                remote_build->set_direction(SOUTH);
                break;
            default:
                break;
            }
        }
    }
    break;
    case df::enums::building_type::RoadDirt:
        break;
    case df::enums::building_type::RoadPaved:
        break;
    case df::enums::building_type::SiegeEngine:
    {
        auto actual = strict_virtual_cast<df::building_siegeenginest>(local_build);
        if (actual)
        {
            auto facing = actual->facing;
            switch (facing)
            {
            case df::building_siegeenginest::Left:
                remote_build->set_direction(WEST);
                break;
            case df::building_siegeenginest::Up:
                remote_build->set_direction(NORTH);
                break;
            case df::building_siegeenginest::Right:
                remote_build->set_direction(EAST);
                break;
            case df::building_siegeenginest::Down:
                remote_build->set_direction(SOUTH);
                break;
            default:
                break;
            }
        }
    }
    break;
    case df::enums::building_type::Trap:
        break;
    case df::enums::building_type::AnimalTrap:
        break;
    case df::enums::building_type::Support:
        break;
    case df::enums::building_type::ArcheryTarget:
        break;
    case df::enums::building_type::Chain:
        break;
    case df::enums::building_type::Cage:
        break;
    case df::enums::building_type::Stockpile:
        break;
    case df::enums::building_type::Civzone:
        break;
    case df::enums::building_type::Weapon:
        break;
    case df::enums::building_type::Wagon:
        break;
    case df::enums::building_type::ScrewPump:
    {
        auto actual = strict_virtual_cast<df::building_screw_pumpst>(local_build);
        if (actual)
        {
            auto direction = actual->direction;
            switch (direction)
            {
            case df::enums::screw_pump_direction::FromNorth:
                remote_build->set_direction(NORTH);
                break;
            case df::enums::screw_pump_direction::FromEast:
                remote_build->set_direction(EAST);
                break;
            case df::enums::screw_pump_direction::FromSouth:
                remote_build->set_direction(SOUTH);
                break;
            case df::enums::screw_pump_direction::FromWest:
                remote_build->set_direction(WEST);
                break;
            default:
                break;
            }
        }
    }
    break;
    case df::enums::building_type::Construction:
        break;
    case df::enums::building_type::Hatch:
        break;
    case df::enums::building_type::GrateWall:
        break;
    case df::enums::building_type::GrateFloor:
        break;
    case df::enums::building_type::BarsVertical:
        break;
    case df::enums::building_type::BarsFloor:
        break;
    case df::enums::building_type::GearAssembly:
        break;
    case df::enums::building_type::AxleHorizontal:
    {
        auto actual = strict_virtual_cast<df::building_axle_horizontalst>(local_build);
        if (actual)
        {
            if(actual->is_vertical)
                remote_build->set_direction(NORTH);
            else
                remote_build->set_direction(EAST);
        }
    }
    break;
    case df::enums::building_type::AxleVertical:
        break;
    case df::enums::building_type::WaterWheel:
    {
        auto actual = strict_virtual_cast<df::building_water_wheelst>(local_build);
        if (actual)
        {
            if (actual->is_vertical)
                remote_build->set_direction(NORTH);
            else
                remote_build->set_direction(EAST);
        }
    }
    break;
    case df::enums::building_type::Windmill:
    {
        auto actual = strict_virtual_cast<df::building_windmillst>(local_build);
        if (actual)
        {
            if (actual->orient_x < 0)
                remote_build->set_direction(WEST);
            else if (actual->orient_x > 0)
                remote_build->set_direction(EAST);
            else if (actual->orient_y < 0)
                remote_build->set_direction(NORTH);
            else if (actual->orient_y > 0)
                remote_build->set_direction(SOUTH);
            else
                remote_build->set_direction(WEST);
        }
    }
    break;
    case df::enums::building_type::TractionBench:
        break;
    case df::enums::building_type::Slab:
        break;
    case df::enums::building_type::Nest:
        break;
    case df::enums::building_type::NestBox:
        break;
    case df::enums::building_type::Hive:
        break;
    default:
        break;
    }
}

RemoteFortressReader::TiletypeMaterial TranslateMaterial(df::tiletype_material material)
{
    switch (material)
    {
    case df::enums::tiletype_material::NONE:
        return RemoteFortressReader::NO_MATERIAL;
        break;
    case df::enums::tiletype_material::AIR:
        return RemoteFortressReader::AIR;
        break;
    case df::enums::tiletype_material::SOIL:
        return RemoteFortressReader::SOIL;
        break;
    case df::enums::tiletype_material::STONE:
        return RemoteFortressReader::STONE;
        break;
    case df::enums::tiletype_material::FEATURE:
        return RemoteFortressReader::FEATURE;
        break;
    case df::enums::tiletype_material::LAVA_STONE:
        return RemoteFortressReader::LAVA_STONE;
        break;
    case df::enums::tiletype_material::MINERAL:
        return RemoteFortressReader::MINERAL;
        break;
    case df::enums::tiletype_material::FROZEN_LIQUID:
        return RemoteFortressReader::FROZEN_LIQUID;
        break;
    case df::enums::tiletype_material::CONSTRUCTION:
        return RemoteFortressReader::CONSTRUCTION;
        break;
    case df::enums::tiletype_material::GRASS_LIGHT:
        return RemoteFortressReader::GRASS_LIGHT;
        break;
    case df::enums::tiletype_material::GRASS_DARK:
        return RemoteFortressReader::GRASS_DARK;
        break;
    case df::enums::tiletype_material::GRASS_DRY:
        return RemoteFortressReader::GRASS_DRY;
        break;
    case df::enums::tiletype_material::GRASS_DEAD:
        return RemoteFortressReader::GRASS_DEAD;
        break;
    case df::enums::tiletype_material::PLANT:
        return RemoteFortressReader::PLANT;
        break;
    case df::enums::tiletype_material::HFS:
        return RemoteFortressReader::HFS;
        break;
    case df::enums::tiletype_material::CAMPFIRE:
        return RemoteFortressReader::CAMPFIRE;
        break;
    case df::enums::tiletype_material::FIRE:
        return RemoteFortressReader::FIRE;
        break;
    case df::enums::tiletype_material::ASHES:
        return RemoteFortressReader::ASHES;
        break;
    case df::enums::tiletype_material::MAGMA:
        return RemoteFortressReader::MAGMA;
        break;
    case df::enums::tiletype_material::DRIFTWOOD:
        return RemoteFortressReader::DRIFTWOOD;
        break;
    case df::enums::tiletype_material::POOL:
        return RemoteFortressReader::POOL;
        break;
    case df::enums::tiletype_material::BROOK:
        return RemoteFortressReader::BROOK;
        break;
    case df::enums::tiletype_material::RIVER:
        return RemoteFortressReader::RIVER;
        break;
    default:
        return RemoteFortressReader::NO_MATERIAL;
        break;
    }
    return RemoteFortressReader::NO_MATERIAL;
}

RemoteFortressReader::TiletypeSpecial TranslateSpecial(df::tiletype_special special)
{
    switch (special)
    {
    case df::enums::tiletype_special::NONE:
        return RemoteFortressReader::NO_SPECIAL;
        break;
    case df::enums::tiletype_special::NORMAL:
        return RemoteFortressReader::NORMAL;
        break;
    case df::enums::tiletype_special::RIVER_SOURCE:
        return RemoteFortressReader::RIVER_SOURCE;
        break;
    case df::enums::tiletype_special::WATERFALL:
        return RemoteFortressReader::WATERFALL;
        break;
    case df::enums::tiletype_special::SMOOTH:
        return RemoteFortressReader::SMOOTH;
        break;
    case df::enums::tiletype_special::FURROWED:
        return RemoteFortressReader::FURROWED;
        break;
    case df::enums::tiletype_special::WET:
        return RemoteFortressReader::WET;
        break;
    case df::enums::tiletype_special::DEAD:
        return RemoteFortressReader::DEAD;
        break;
    case df::enums::tiletype_special::WORN_1:
        return RemoteFortressReader::WORN_1;
        break;
    case df::enums::tiletype_special::WORN_2:
        return RemoteFortressReader::WORN_2;
        break;
    case df::enums::tiletype_special::WORN_3:
        return RemoteFortressReader::WORN_3;
        break;
    default:
        return RemoteFortressReader::NO_SPECIAL;
        break;
    }
    return RemoteFortressReader::NO_SPECIAL;
}

RemoteFortressReader::TiletypeShape TranslateShape(df::tiletype_shape shape)
{
    switch (shape)
    {
    case df::enums::tiletype_shape::NONE:
        return RemoteFortressReader::NO_SHAPE;
        break;
    case df::enums::tiletype_shape::EMPTY:
        return RemoteFortressReader::EMPTY;
        break;
    case df::enums::tiletype_shape::FLOOR:
        return RemoteFortressReader::FLOOR;
        break;
    case df::enums::tiletype_shape::BOULDER:
        return RemoteFortressReader::BOULDER;
        break;
    case df::enums::tiletype_shape::PEBBLES:
        return RemoteFortressReader::PEBBLES;
        break;
    case df::enums::tiletype_shape::WALL:
        return RemoteFortressReader::WALL;
        break;
    case df::enums::tiletype_shape::FORTIFICATION:
        return RemoteFortressReader::FORTIFICATION;
        break;
    case df::enums::tiletype_shape::STAIR_UP:
        return RemoteFortressReader::STAIR_UP;
        break;
    case df::enums::tiletype_shape::STAIR_DOWN:
        return RemoteFortressReader::STAIR_DOWN;
        break;
    case df::enums::tiletype_shape::STAIR_UPDOWN:
        return RemoteFortressReader::STAIR_UPDOWN;
        break;
    case df::enums::tiletype_shape::RAMP:
        return RemoteFortressReader::RAMP;
        break;
    case df::enums::tiletype_shape::RAMP_TOP:
        return RemoteFortressReader::RAMP_TOP;
        break;
    case df::enums::tiletype_shape::BROOK_BED:
        return RemoteFortressReader::BROOK_BED;
        break;
    case df::enums::tiletype_shape::BROOK_TOP:
        return RemoteFortressReader::BROOK_TOP;
        break;
    case df::enums::tiletype_shape::SAPLING:
        return RemoteFortressReader::SAPLING;
        break;
    case df::enums::tiletype_shape::SHRUB:
        return RemoteFortressReader::SHRUB;
        break;
    case df::enums::tiletype_shape::ENDLESS_PIT:
        return RemoteFortressReader::EMPTY;
        break;
    default:
        return RemoteFortressReader::NO_SHAPE;
        break;
    }
    return RemoteFortressReader::NO_SHAPE;
}

RemoteFortressReader::TiletypeVariant TranslateVariant(df::tiletype_variant variant)
{
    switch (variant)
    {
    case df::enums::tiletype_variant::NONE:
        return RemoteFortressReader::NO_VARIANT;
        break;
    case df::enums::tiletype_variant::VAR_1:
        return RemoteFortressReader::VAR_1;
        break;
    case df::enums::tiletype_variant::VAR_2:
        return RemoteFortressReader::VAR_2;
        break;
    case df::enums::tiletype_variant::VAR_3:
        return RemoteFortressReader::VAR_3;
        break;
    case df::enums::tiletype_variant::VAR_4:
        return RemoteFortressReader::VAR_4;
        break;
    default:
        return RemoteFortressReader::NO_VARIANT;
        break;
    }
    return RemoteFortressReader::NO_VARIANT;
}

static command_result CheckHashes(color_ostream &stream, const EmptyMessage *in)
{
    clock_t start = clock();
    for (int i = 0; i < world->map.map_blocks.size(); i++)
    {
        df::map_block * block = world->map.map_blocks[i];
        fletcher16((uint8_t*)(block->tiletype), 16 * 16 * sizeof(df::enums::tiletype::tiletype));
    }
    clock_t end = clock();
    double elapsed_secs = double(end - start) / CLOCKS_PER_SEC;
    stream.print("Checking all hashes took %f seconds.", elapsed_secs);
    return CR_OK;
}

map<DFCoord, uint16_t> hashes;

//check if the tiletypes have changed
bool IsTiletypeChanged(DFCoord pos)
{
    uint16_t hash;
    df::map_block * block = Maps::getBlock(pos);
    if (block)
        hash = fletcher16((uint8_t*)(block->tiletype), 16 * 16 * (sizeof(df::enums::tiletype::tiletype)));
    else
        hash = 0;
    if (hashes[pos] != hash)
    {
        hashes[pos] = hash;
        return true;
    }
    return false;
}

map<DFCoord, uint16_t> waterHashes;

//check if the designations have changed
bool IsDesignationChanged(DFCoord pos)
{
    uint16_t hash;
    df::map_block * block = Maps::getBlock(pos);
    if (block)
        hash = fletcher16((uint8_t*)(block->designation), 16 * 16 * (sizeof(df::tile_designation)));
    else
        hash = 0;
    if (waterHashes[pos] != hash)
    {
        waterHashes[pos] = hash;
        return true;
    }
    return false;
}

map<DFCoord, uint8_t> buildingHashes;

//check if the designations have changed
bool IsBuildingChanged(DFCoord pos)
{
    df::map_block * block = Maps::getBlock(pos);
    bool changed = false;
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
        {
            DFCoord localPos = DFCoord(pos.x * 16 + x, pos.y * 16 + y, pos.z);
            auto bld = block->occupancy[x][y].bits.building;
            if (buildingHashes[pos] != bld)
            {
                buildingHashes[pos] = bld;
                changed = true;
            }
        }
    return changed;
}

static command_result ResetMapHashes(color_ostream &stream, const EmptyMessage *in)
{
    hashes.clear();
    waterHashes.clear();
    buildingHashes.clear();
    return CR_OK;
}

df::matter_state GetState(df::material * mat, uint16_t temp = 10015)
{
    df::matter_state state = matter_state::Solid;
    if (temp >= mat->heat.melting_point)
        state = df::matter_state::Liquid;
    if (temp >= mat->heat.boiling_point)
        state = matter_state::Gas;
    return state;
}

static command_result GetMaterialList(color_ostream &stream, const EmptyMessage *in, MaterialList *out)
{
    if (!Core::getInstance().isWorldLoaded()) {
        //out->set_available(false);
        return CR_OK;
    }



    df::world_raws *raws = &world->raws;
    MaterialInfo mat;
    for (int i = 0; i < raws->inorganics.all.size(); i++)
    {
        mat.decode(0, i);
        MaterialDefinition *mat_def = out->add_material_list();
        mat_def->mutable_mat_pair()->set_mat_type(0);
        mat_def->mutable_mat_pair()->set_mat_index(i);
        mat_def->set_id(mat.getToken());
        mat_def->set_name(mat.toString()); //find the name at cave temperature;
        if (raws->inorganics.all[i]->material.state_color[GetState(&raws->inorganics.all[i]->material)] < raws->descriptors.colors.size())
        {
            df::descriptor_color *color = raws->descriptors.colors[raws->inorganics.all[i]->material.state_color[GetState(&raws->inorganics.all[i]->material)]];
            mat_def->mutable_state_color()->set_red(color->red * 255);
            mat_def->mutable_state_color()->set_green(color->green * 255);
            mat_def->mutable_state_color()->set_blue(color->blue * 255);
        }
    }
    for (int i = 0; i < 19; i++)
    {
        int k = -1;
        if (i == 7)
            k = 1;// for coal.
        for (int j = -1; j <= k; j++)
        {
            mat.decode(i, j);
            MaterialDefinition *mat_def = out->add_material_list();
            mat_def->mutable_mat_pair()->set_mat_type(i);
            mat_def->mutable_mat_pair()->set_mat_index(j);
            mat_def->set_id(mat.getToken());
            mat_def->set_name(mat.toString()); //find the name at cave temperature;
            if (raws->mat_table.builtin[i]->state_color[GetState(raws->mat_table.builtin[i])] < raws->descriptors.colors.size())
            {
                df::descriptor_color *color = raws->descriptors.colors[raws->mat_table.builtin[i]->state_color[GetState(raws->mat_table.builtin[i])]];
                mat_def->mutable_state_color()->set_red(color->red * 255);
                mat_def->mutable_state_color()->set_green(color->green * 255);
                mat_def->mutable_state_color()->set_blue(color->blue * 255);
            }
        }
    }
    for (int i = 0; i < raws->creatures.all.size(); i++)
    {
        df::creature_raw * creature = raws->creatures.all[i];
        for (int j = 0; j < creature->material.size(); j++)
        {
            mat.decode(j + 19, i);
            MaterialDefinition *mat_def = out->add_material_list();
            mat_def->mutable_mat_pair()->set_mat_type(j + 19);
            mat_def->mutable_mat_pair()->set_mat_index(i);
            mat_def->set_id(mat.getToken());
            mat_def->set_name(mat.toString()); //find the name at cave temperature;
            if (creature->material[j]->state_color[GetState(creature->material[j])] < raws->descriptors.colors.size())
            {
                df::descriptor_color *color = raws->descriptors.colors[creature->material[j]->state_color[GetState(creature->material[j])]];
                mat_def->mutable_state_color()->set_red(color->red * 255);
                mat_def->mutable_state_color()->set_green(color->green * 255);
                mat_def->mutable_state_color()->set_blue(color->blue * 255);
            }
        }
    }
    for (int i = 0; i < raws->plants.all.size(); i++)
    {
        df::plant_raw * plant = raws->plants.all[i];
        for (int j = 0; j < plant->material.size(); j++)
        {
            mat.decode(j + 419, i);
            MaterialDefinition *mat_def = out->add_material_list();
            mat_def->mutable_mat_pair()->set_mat_type(j + 419);
            mat_def->mutable_mat_pair()->set_mat_index(i);
            mat_def->set_id(mat.getToken());
            mat_def->set_name(mat.toString()); //find the name at cave temperature;
            if (plant->material[j]->state_color[GetState(plant->material[j])] < raws->descriptors.colors.size())
            {
                df::descriptor_color *color = raws->descriptors.colors[plant->material[j]->state_color[GetState(plant->material[j])]];
                mat_def->mutable_state_color()->set_red(color->red * 255);
                mat_def->mutable_state_color()->set_green(color->green * 255);
                mat_def->mutable_state_color()->set_blue(color->blue * 255);
            }
        }
    }
    return CR_OK;
}

static command_result GetItemList(color_ostream &stream, const EmptyMessage *in, MaterialList *out)
{
    if (!Core::getInstance().isWorldLoaded()) {
        //out->set_available(false);
        return CR_OK;
    }
    FOR_ENUM_ITEMS(item_type, it)
    {
        MaterialDefinition *mat_def = out->add_material_list();
        mat_def->mutable_mat_pair()->set_mat_type((int)it);
        mat_def->mutable_mat_pair()->set_mat_index(-1);
        mat_def->set_id(ENUM_KEY_STR(item_type, it));
        int subtypes = Items::getSubtypeCount(it);
        if (subtypes >= 0)
        {
            for (int i = 0; i < subtypes; i++)
            {
                mat_def = out->add_material_list();
                mat_def->mutable_mat_pair()->set_mat_type((int)it);
                mat_def->mutable_mat_pair()->set_mat_index(i);
                df::itemdef * item = Items::getSubtypeDef(it, i);
                mat_def->set_id(item->id);
            }
        }
    }


    return CR_OK;
}

static command_result GetGrowthList(color_ostream &stream, const EmptyMessage *in, MaterialList *out)
{
    if (!Core::getInstance().isWorldLoaded()) {
        //out->set_available(false);
        return CR_OK;
    }



    df::world_raws *raws = &world->raws;
    if (!raws)
        return CR_OK;//'.


    for (int i = 0; i < raws->plants.all.size(); i++)
    {
        df::plant_raw * pp = raws->plants.all[i];
        if (!pp)
            continue;
        MaterialDefinition * basePlant = out->add_material_list();
        basePlant->set_id(pp->id + ":BASE");
        basePlant->set_name(pp->name);
        basePlant->mutable_mat_pair()->set_mat_type(-1);
        basePlant->mutable_mat_pair()->set_mat_index(i);
    }
    return CR_OK;
}

void CopyBlock(df::map_block * DfBlock, RemoteFortressReader::MapBlock * NetBlock, MapExtras::MapCache * MC, DFCoord pos)
{
    NetBlock->set_map_x(DfBlock->map_pos.x);
    NetBlock->set_map_y(DfBlock->map_pos.y);
    NetBlock->set_map_z(DfBlock->map_pos.z);

    MapExtras::Block * block = MC->BlockAtTile(DfBlock->map_pos);

    for (int yy = 0; yy < 16; yy++)
        for (int xx = 0; xx < 16; xx++)
        {
            df::tiletype tile = DfBlock->tiletype[xx][yy];
            NetBlock->add_tiles(tile);
            df::coord2d p = df::coord2d(xx, yy);
            t_matpair baseMat = block->baseMaterialAt(p);
            t_matpair staticMat = block->staticMaterialAt(p);
            switch (tileMaterial(tile))
            {
            case tiletype_material::FROZEN_LIQUID:
                staticMat.mat_type = builtin_mats::WATER;
                staticMat.mat_index = -1;
                break;
            default:
                break;
            }
            RemoteFortressReader::MatPair * material = NetBlock->add_materials();
            material->set_mat_type(staticMat.mat_type);
            material->set_mat_index(staticMat.mat_index);
            RemoteFortressReader::MatPair * layerMaterial = NetBlock->add_layer_materials();
            layerMaterial->set_mat_type(0);
            layerMaterial->set_mat_index(block->layerMaterialAt(p));
            RemoteFortressReader::MatPair * veinMaterial = NetBlock->add_vein_materials();
            veinMaterial->set_mat_type(0);
            veinMaterial->set_mat_index(block->veinMaterialAt(p));
            RemoteFortressReader::MatPair * baseMaterial = NetBlock->add_base_materials();
            baseMaterial->set_mat_type(baseMat.mat_type);
            baseMaterial->set_mat_index(baseMat.mat_index);
            RemoteFortressReader::MatPair * constructionItem = NetBlock->add_construction_items();
            constructionItem->set_mat_type(-1);
            constructionItem->set_mat_index(-1);
            if (tileMaterial(tile) == tiletype_material::CONSTRUCTION)
            {
                df::construction *con = df::construction::find(DfBlock->map_pos + df::coord(xx, yy, 0));
                if (con)
                {
                    constructionItem->set_mat_type(con->item_type);
                    constructionItem->set_mat_index(con->item_subtype);
                }
            }
        }
}

void CopyDesignation(df::map_block * DfBlock, RemoteFortressReader::MapBlock * NetBlock, MapExtras::MapCache * MC, DFCoord pos)
{
    NetBlock->set_map_x(DfBlock->map_pos.x);
    NetBlock->set_map_y(DfBlock->map_pos.y);
    NetBlock->set_map_z(DfBlock->map_pos.z);

    for (int yy = 0; yy < 16; yy++)
        for (int xx = 0; xx < 16; xx++)
        {
            df::tile_designation designation = DfBlock->designation[xx][yy];
            int lava = 0;
            int water = 0;
            if (designation.bits.liquid_type == df::enums::tile_liquid::Magma)
                lava = designation.bits.flow_size;
            else
                water = designation.bits.flow_size;
            NetBlock->add_magma(lava);
            NetBlock->add_water(water);
            NetBlock->add_aquifer(designation.bits.water_table);
            NetBlock->add_light(designation.bits.light);
            NetBlock->add_outside(designation.bits.outside);
            NetBlock->add_subterranean(designation.bits.subterranean);
            NetBlock->add_water_salt(designation.bits.water_salt);
            NetBlock->add_water_stagnant(designation.bits.water_stagnant);
            if (gamemode && (*gamemode == game_mode::ADVENTURE))
            {
                auto fog_of_war = DfBlock->fog_of_war[xx][yy];
                NetBlock->add_hidden(designation.bits.dig == TileDigDesignation::NO_DIG || designation.bits.hidden);
                NetBlock->add_tile_dig_designation(TileDigDesignation::NO_DIG);
            }
            else
            {
                NetBlock->add_hidden(designation.bits.hidden);
                switch (designation.bits.dig)
                {
                case df::enums::tile_dig_designation::No:
                    NetBlock->add_tile_dig_designation(TileDigDesignation::NO_DIG);
                    break;
                case df::enums::tile_dig_designation::Default:
                    NetBlock->add_tile_dig_designation(TileDigDesignation::DEFAULT_DIG);
                    break;
                case df::enums::tile_dig_designation::UpDownStair:
                    NetBlock->add_tile_dig_designation(TileDigDesignation::UP_DOWN_STAIR_DIG);
                    break;
                case df::enums::tile_dig_designation::Channel:
                    NetBlock->add_tile_dig_designation(TileDigDesignation::CHANNEL_DIG);
                    break;
                case df::enums::tile_dig_designation::Ramp:
                    NetBlock->add_tile_dig_designation(TileDigDesignation::RAMP_DIG);
                    break;
                case df::enums::tile_dig_designation::DownStair:
                    NetBlock->add_tile_dig_designation(TileDigDesignation::DOWN_STAIR_DIG);
                    break;
                case df::enums::tile_dig_designation::UpStair:
                    NetBlock->add_tile_dig_designation(TileDigDesignation::UP_STAIR_DIG);
                    break;
                default:
                    NetBlock->add_tile_dig_designation(TileDigDesignation::NO_DIG);
                    break;
                }
            }
        }
}

void CopyBuildings(df::map_block * DfBlock, RemoteFortressReader::MapBlock * NetBlock, MapExtras::MapCache * MC, DFCoord pos)
{
    int minX = DfBlock->map_pos.x;
    int minY = DfBlock->map_pos.y;
    int Z = DfBlock->map_pos.z;

    int maxX = minX + 15;
    int maxY = minY + 15;

    for (int i = 0; i < df::global::world->buildings.all.size(); i++)
    {
        df::building * bld = df::global::world->buildings.all[i];
        if (bld->x1 > maxX || bld->y1 > maxY || bld->x2 < minX || bld->y2 < minY)
            continue;

        int z2 = bld->z;

        if (bld->getType() == building_type::Well)
        {
            df::building_wellst * well_building = virtual_cast<df::building_wellst>(bld);
            if (well_building)
            {
                z2 = well_building->bucket_z;
            }
        }
        if (bld->z < Z || z2 > Z)
            continue;
        auto out_bld = NetBlock->add_buildings();
        CopyBuilding(i, out_bld);
    }
}

static command_result GetBlockList(color_ostream &stream, const BlockRequest *in, BlockList *out)
{
    int x, y, z;
    DFHack::Maps::getPosition(x, y, z);
    out->set_map_x(x);
    out->set_map_y(y);
    MapExtras::MapCache MC;
    int center_x = (in->min_x() + in->max_x()) / 2;
    int center_y = (in->min_y() + in->max_y()) / 2;

    int NUMBER_OF_POINTS = ((in->max_x() - center_x + 1) * 2) * ((in->max_y() - center_y + 1) * 2);
    int blocks_needed;
    if (in->has_blocks_needed())
        blocks_needed = in->blocks_needed();
    else
        blocks_needed = NUMBER_OF_POINTS*(in->max_z() - in->min_z());
    int blocks_sent = 0;
    int min_x = in->min_x();
    int min_y = in->min_y();
    int max_x = in->max_x();
    int max_y = in->max_y();
    //stream.print("Got request for blocks from (%d, %d, %d) to (%d, %d, %d).\n", in->min_x(), in->min_y(), in->min_z(), in->max_x(), in->max_y(), in->max_z());
    for (int zz = in->max_z() - 1; zz >= in->min_z(); zz--)
    {
        // (di, dj) is a vector - direction in which we move right now
        int di = 1;
        int dj = 0;
        // length of current segment
        int segment_length = 1;
        // current position (i, j) and how much of current segment we passed
        int i = center_x;
        int j = center_y;
        int segment_passed = 0;
        for (int k = 0; k < NUMBER_OF_POINTS; ++k)
        {
            if (blocks_sent >= blocks_needed)
                break;
            if (!(i < min_x || i >= max_x || j < min_y || j >= max_y))
            {
                DFCoord pos = DFCoord(i, j, zz);
                df::map_block * block = DFHack::Maps::getBlock(pos);
                if (block != NULL)
                {
                    int nonAir = 0;
                    for (int xxx = 0; xxx < 16; xxx++)
                        for (int yyy = 0; yyy < 16; yyy++)
                        {
                            if ((DFHack::tileShapeBasic(DFHack::tileShape(block->tiletype[xxx][yyy])) != df::tiletype_shape_basic::None &&
                                DFHack::tileShapeBasic(DFHack::tileShape(block->tiletype[xxx][yyy])) != df::tiletype_shape_basic::Open)
                                || block->designation[xxx][yyy].bits.flow_size > 0
                                || block->occupancy[xxx][yyy].bits.building > 0)
                                nonAir++;
                        }
                    if (nonAir > 0)
                    {
                        bool tileChanged = IsTiletypeChanged(pos);
                        bool desChanged = IsDesignationChanged(pos);
                        //bool bldChanged = IsBuildingChanged(pos);
                        RemoteFortressReader::MapBlock *net_block;
                        if (tileChanged || desChanged)
                            net_block = out->add_map_blocks();
                        if (tileChanged)
                        {
                            CopyBlock(block, net_block, &MC, pos);
                            blocks_sent++;
                        }
                        if (desChanged)
                            CopyDesignation(block, net_block, &MC, pos);
                        if (tileChanged)
                        {
                            CopyBuildings(block, net_block, &MC, pos);
                        }
                    }
                }
            }

            // make a step, add 'direction' vector (di, dj) to current position (i, j)
            i += di;
            j += dj;
            ++segment_passed;
            //System.out.println(i + " " + j);

            if (segment_passed == segment_length)
            {
                // done with current segment
                segment_passed = 0;

                // 'rotate' directions
                int buffer = di;
                di = -dj;
                dj = buffer;

                // increase segment length if necessary
                if (dj == 0) {
                    ++segment_length;
                }
            }
        }
        //for (int yy = in->min_y(); yy < in->max_y(); yy++)
        //{
        //    for (int xx = in->min_x(); xx < in->max_x(); xx++)
        //    {
        //        DFCoord pos = DFCoord(xx, yy, zz);
        //        df::map_block * block = DFHack::Maps::getBlock(pos);
        //        if (block == NULL)
        //            continue;
        //        {
        //            RemoteFortressReader::MapBlock *net_block = out->add_map_blocks();
        //            CopyBlock(block, net_block, &MC, pos);
        //        }
        //    }
        //}
    }
    MC.trash();
    return CR_OK;
}

static command_result GetTiletypeList(color_ostream &stream, const EmptyMessage *in, TiletypeList *out)
{
    int count = 0;
    FOR_ENUM_ITEMS(tiletype, tt)
    {
        Tiletype * type = out->add_tiletype_list();
        type->set_id(tt);
        type->set_name(ENUM_KEY_STR(tiletype, tt));
        const char * name = tileName(tt);
        if (name != NULL && name[0] != 0)
            type->set_caption(name);
        type->set_shape(TranslateShape(tileShape(tt)));
        type->set_special(TranslateSpecial(tileSpecial(tt)));
        type->set_material(TranslateMaterial(tileMaterial(tt)));
        type->set_variant(TranslateVariant(tileVariant(tt)));
        type->set_direction(tileDirection(tt).getStr());
        count++;
    }
    return CR_OK;
}

static command_result GetPlantList(color_ostream &stream, const BlockRequest *in, PlantList *out)
{
    int min_x = in->min_x() / 3;
    int min_y = in->min_y() / 3;
    int min_z = in->min_z();
    int max_x = in->max_x() / 3;
    int max_y = in->max_y() / 3;
    int max_z = in->max_z();

    for (int xx = min_x; xx < max_x; xx++)
        for (int yy = min_y; yy < max_y; yy++)
        {
            for (int zz = min_z; zz < max_z; zz++)
            {
                if (xx < 0 || yy < 0 || zz < 0 || xx >= world->map.x_count || yy >= world->map.y_count || zz >= world->map.z_count)
                    continue;
                df::map_block * block = world->map.block_index[xx][yy][zz];
                for (int i = 0; i < block->plants.size(); i++)
                {
                    df::plant * plant = block->plants[i];
                    if (plant->pos.z < min_z || plant->pos.z >= max_z)
                        continue;
                    if (plant->pos.x < in->min_x() * 16 || plant->pos.x >= in->max_x() * 16)
                        continue;
                    if (plant->pos.y < in->min_y() * 16 || plant->pos.y >= in->max_y() * 16)
                        continue;
                    RemoteFortressReader::PlantDef * out_plant = out->add_plant_list();
                    out_plant->set_index(plant->material);
                    out_plant->set_pos_x(plant->pos.x);
                    out_plant->set_pos_y(plant->pos.y);
                    out_plant->set_pos_z(plant->pos.z);
                }
            }
        }
    return CR_OK;
}

static command_result GetUnitList(color_ostream &stream, const EmptyMessage *in, UnitList *out)
{
    auto world = df::global::world;
    for (int i = 0; i < world->units.all.size(); i++)
    {
        df::unit * unit = world->units.all[i];
        auto send_unit = out->add_creature_list();
        send_unit->set_id(unit->id);
        send_unit->set_pos_x(unit->pos.x);
        send_unit->set_pos_y(unit->pos.y);
        send_unit->set_pos_z(unit->pos.z);
        send_unit->mutable_race()->set_mat_type(unit->race);
        send_unit->mutable_race()->set_mat_index(unit->caste);
        ConvertDfColor(Units::getProfessionColor(unit), send_unit->mutable_profession_color());
        send_unit->set_flags1(unit->flags1.whole);
        send_unit->set_flags2(unit->flags2.whole);
        send_unit->set_flags3(unit->flags3.whole);
        send_unit->set_is_soldier(ENUM_ATTR(profession, military, unit->profession));
        auto size_info = send_unit->mutable_size_info();
        size_info->set_size_cur(unit->body.size_info.size_cur);
        size_info->set_size_base(unit->body.size_info.size_base);
        size_info->set_area_cur(unit->body.size_info.area_cur);
        size_info->set_area_base(unit->body.size_info.area_base);
        size_info->set_length_cur(unit->body.size_info.length_cur);
        size_info->set_length_base(unit->body.size_info.length_base);
        if (unit->name.has_name)
        {
            send_unit->set_name(DF2UTF(Translation::TranslateName(Units::getVisibleName(unit))));
        }

        auto appearance = send_unit->mutable_appearance();
        for (int j = 0; j < unit->appearance.body_modifiers.size(); j++)
            appearance->add_body_modifiers(unit->appearance.body_modifiers[j]);
        for (int j = 0; j < unit->appearance.bp_modifiers.size(); j++)
            appearance->add_bp_modifiers(unit->appearance.bp_modifiers[j]);
        for (int j = 0; j < unit->appearance.colors.size(); j++)
            appearance->add_colors(unit->appearance.colors[j]);
        appearance->set_size_modifier(unit->appearance.size_modifier);
    }
    return CR_OK;
}

static command_result GetViewInfo(color_ostream &stream, const EmptyMessage *in, ViewInfo *out)
{
    int x, y, z, w, h, cx, cy, cz;
    Gui::getWindowSize(w, h);
    Gui::getViewCoords(x, y, z);
    Gui::getCursorCoords(cx, cy, cz);

    auto embark = Gui::getViewscreenByType<df::viewscreen_choose_start_sitest>(0);
    if (embark)
    {
        df::starter_infost location = embark->location;
        df::world_data * data = df::global::world->world_data;
        if (data && data->region_map)
        {
            z = data->region_map[location.region_pos.x][location.region_pos.y].elevation;
        }
    }

    out->set_view_pos_x(x);
    out->set_view_pos_y(y);
    out->set_view_pos_z(z);
    out->set_view_size_x(w);
    out->set_view_size_y(h);
    out->set_cursor_pos_x(cx);
    out->set_cursor_pos_y(cy);
    out->set_cursor_pos_z(cz);
    return CR_OK;
}

static command_result GetMapInfo(color_ostream &stream, const EmptyMessage *in, MapInfo *out)
{
    if (!Maps::IsValid())
        return CR_FAILURE;
    uint32_t size_x, size_y, size_z;
    int32_t pos_x, pos_y, pos_z;
    Maps::getSize(size_x, size_y, size_z);
    Maps::getPosition(pos_x, pos_y, pos_z);
    out->set_block_size_x(size_x);
    out->set_block_size_y(size_y);
    out->set_block_size_z(size_z);
    out->set_block_pos_x(pos_x);
    out->set_block_pos_y(pos_y);
    out->set_block_pos_z(pos_z);
    out->set_world_name(DF2UTF(Translation::TranslateName(&df::global::world->world_data->name, false)));
    out->set_world_name_english(DF2UTF(Translation::TranslateName(&df::global::world->world_data->name, true)));
    out->set_save_name(df::global::world->cur_savegame.save_dir);
    return CR_OK;
}

static command_result GetBuildingDefList(color_ostream &stream, const EmptyMessage *in, BuildingList *out)
{
    FOR_ENUM_ITEMS(building_type, bt)
    {
        BuildingDefinition * bld = out->add_building_list();
        bld->mutable_building_type()->set_building_type(bt);
        bld->mutable_building_type()->set_building_subtype(-1);
        bld->mutable_building_type()->set_building_custom(-1);
        bld->set_id(ENUM_KEY_STR(building_type, bt));

        switch (bt)
        {
        case df::enums::building_type::NONE:
            break;
        case df::enums::building_type::Chair:
            break;
        case df::enums::building_type::Bed:
            break;
        case df::enums::building_type::Table:
            break;
        case df::enums::building_type::Coffin:
            break;
        case df::enums::building_type::FarmPlot:
            break;
        case df::enums::building_type::Furnace:
            FOR_ENUM_ITEMS(furnace_type, st)
            {
                bld = out->add_building_list();
                bld->mutable_building_type()->set_building_type(bt);
                bld->mutable_building_type()->set_building_subtype(st);
                bld->mutable_building_type()->set_building_custom(-1);
                bld->set_id(ENUM_KEY_STR(building_type, bt) + "_" + ENUM_KEY_STR(furnace_type, st));

                if (st == furnace_type::Custom)
                {
                    for (int i = 0; i < world->raws.buildings.furnaces.size(); i++)
                    {
                        auto cust = world->raws.buildings.furnaces[i];

                        bld = out->add_building_list();
                        bld->mutable_building_type()->set_building_type(bt);
                        bld->mutable_building_type()->set_building_subtype(st);
                        bld->mutable_building_type()->set_building_custom(cust->id);
                        bld->set_id(cust->code);
                        bld->set_name(cust->name);
                    }
                }
            }
            break;
        case df::enums::building_type::TradeDepot:
            break;
        case df::enums::building_type::Shop:
            FOR_ENUM_ITEMS(shop_type, st)
            {
                bld = out->add_building_list();
                bld->mutable_building_type()->set_building_type(bt);
                bld->mutable_building_type()->set_building_subtype(st);
                bld->mutable_building_type()->set_building_custom(-1);
                bld->set_id(ENUM_KEY_STR(building_type, bt) + "_" + ENUM_KEY_STR(shop_type, st));

            }
            break;
        case df::enums::building_type::Door:
            break;
        case df::enums::building_type::Floodgate:
            break;
        case df::enums::building_type::Box:
            break;
        case df::enums::building_type::Weaponrack:
            break;
        case df::enums::building_type::Armorstand:
            break;
        case df::enums::building_type::Workshop:
            FOR_ENUM_ITEMS(workshop_type, st)
            {
                bld = out->add_building_list();
                bld->mutable_building_type()->set_building_type(bt);
                bld->mutable_building_type()->set_building_subtype(st);
                bld->mutable_building_type()->set_building_custom(-1);
                bld->set_id(ENUM_KEY_STR(building_type, bt) + "_" + ENUM_KEY_STR(workshop_type, st));

                if (st == workshop_type::Custom)
                {
                    for (int i = 0; i < world->raws.buildings.workshops.size(); i++)
                    {
                        auto cust = world->raws.buildings.workshops[i];

                        bld = out->add_building_list();
                        bld->mutable_building_type()->set_building_type(bt);
                        bld->mutable_building_type()->set_building_subtype(st);
                        bld->mutable_building_type()->set_building_custom(cust->id);
                        bld->set_id(cust->code);
                        bld->set_name(cust->name);
                    }
                }
            }
            break;
        case df::enums::building_type::Cabinet:
            break;
        case df::enums::building_type::Statue:
            break;
        case df::enums::building_type::WindowGlass:
            break;
        case df::enums::building_type::WindowGem:
            break;
        case df::enums::building_type::Well:
            break;
        case df::enums::building_type::Bridge:
            break;
        case df::enums::building_type::RoadDirt:
            break;
        case df::enums::building_type::RoadPaved:
            break;
        case df::enums::building_type::SiegeEngine:
            FOR_ENUM_ITEMS(siegeengine_type, st)
            {
                bld = out->add_building_list();
                bld->mutable_building_type()->set_building_type(bt);
                bld->mutable_building_type()->set_building_subtype(st);
                bld->mutable_building_type()->set_building_custom(-1);
                bld->set_id(ENUM_KEY_STR(building_type, bt) + "_" + ENUM_KEY_STR(siegeengine_type, st));

            }
            break;
        case df::enums::building_type::Trap:
            FOR_ENUM_ITEMS(trap_type, st)
            {
                bld = out->add_building_list();
                bld->mutable_building_type()->set_building_type(bt);
                bld->mutable_building_type()->set_building_subtype(st);
                bld->mutable_building_type()->set_building_custom(-1);
                bld->set_id(ENUM_KEY_STR(building_type, bt) + "_" + ENUM_KEY_STR(trap_type, st));

            }
            break;
        case df::enums::building_type::AnimalTrap:
            break;
        case df::enums::building_type::Support:
            break;
        case df::enums::building_type::ArcheryTarget:
            break;
        case df::enums::building_type::Chain:
            break;
        case df::enums::building_type::Cage:
            break;
        case df::enums::building_type::Stockpile:
            break;
        case df::enums::building_type::Civzone:
            FOR_ENUM_ITEMS(civzone_type, st)
            {
                bld = out->add_building_list();
                bld->mutable_building_type()->set_building_type(bt);
                bld->mutable_building_type()->set_building_subtype(st);
                bld->mutable_building_type()->set_building_custom(-1);
                bld->set_id(ENUM_KEY_STR(building_type, bt) + "_" + ENUM_KEY_STR(civzone_type, st));

            }
            break;
        case df::enums::building_type::Weapon:
            break;
        case df::enums::building_type::Wagon:
            break;
        case df::enums::building_type::ScrewPump:
            break;
        case df::enums::building_type::Construction:
            FOR_ENUM_ITEMS(construction_type, st)
            {
                bld = out->add_building_list();
                bld->mutable_building_type()->set_building_type(bt);
                bld->mutable_building_type()->set_building_subtype(st);
                bld->mutable_building_type()->set_building_custom(-1);
                bld->set_id(ENUM_KEY_STR(building_type, bt) + "_" + ENUM_KEY_STR(construction_type, st));

            }
            break;
        case df::enums::building_type::Hatch:
            break;
        case df::enums::building_type::GrateWall:
            break;
        case df::enums::building_type::GrateFloor:
            break;
        case df::enums::building_type::BarsVertical:
            break;
        case df::enums::building_type::BarsFloor:
            break;
        case df::enums::building_type::GearAssembly:
            break;
        case df::enums::building_type::AxleHorizontal:
            break;
        case df::enums::building_type::AxleVertical:
            break;
        case df::enums::building_type::WaterWheel:
            break;
        case df::enums::building_type::Windmill:
            break;
        case df::enums::building_type::TractionBench:
            break;
        case df::enums::building_type::Slab:
            break;
        case df::enums::building_type::Nest:
            break;
        case df::enums::building_type::NestBox:
            break;
        case df::enums::building_type::Hive:
            break;
        default:
            break;
        }
    }
    return CR_OK;
}

DFCoord GetMapCenter()
{
    DFCoord output;
    auto embark = Gui::getViewscreenByType<df::viewscreen_choose_start_sitest>(0);
    if (embark)
    {
        df::starter_infost location = embark->location;
        output.x = (location.region_pos.x * 16) + 8;
        output.y = (location.region_pos.y * 16) + 8;
        output.z = 100;
        df::world_data * data = df::global::world->world_data;
        if (data && data->region_map)
        {
            output.z = data->region_map[location.region_pos.x][location.region_pos.y].elevation;
        }
    }
    else if (Maps::IsValid())
    {
        int x, y, z;
        Maps::getPosition(x,y,z);
        output = DFCoord(x, y, z);
    }
    return output;
}

static command_result GetWorldMapCenter(color_ostream &stream, const EmptyMessage *in, WorldMap *out)
{
    if (!df::global::world->world_data)
    {
        out->set_world_width(0);
        out->set_world_height(0);
        return CR_FAILURE;
    }
    df::world_data * data = df::global::world->world_data;
    int width = data->world_width;
    int height = data->world_height;
    out->set_world_width(width);
    out->set_world_height(height);
    DFCoord pos = GetMapCenter();
    out->set_center_x(pos.x);
    out->set_center_y(pos.y);
    out->set_center_z(pos.z);
    out->set_name(Translation::TranslateName(&(data->name), false));
    out->set_name_english(Translation::TranslateName(&(data->name), true));
    out->set_cur_year(World::ReadCurrentYear());
    out->set_cur_year_tick(World::ReadCurrentTick());
    return CR_OK;
}

static command_result GetWorldMap(color_ostream &stream, const EmptyMessage *in, WorldMap *out)
{
    if (!df::global::world->world_data)
    {
        out->set_world_width(0);
        out->set_world_height(0);
        return CR_FAILURE;
    }
    df::world_data * data = df::global::world->world_data;
    if (!data->region_map)
    {
        out->set_world_width(0);
        out->set_world_height(0);
        return CR_FAILURE;
    }
    int width = data->world_width;
    int height = data->world_height;
    out->set_world_width(width);
    out->set_world_height(height);
    out->set_name(Translation::TranslateName(&(data->name), false));
    out->set_name_english(Translation::TranslateName(&(data->name), true));
    auto poles = data->flip_latitude;
    switch (poles)
    {
    case df::world_data::North:
        out->set_world_poles(WorldPoles::NORTH_POLE);
        break;
    case df::world_data::South:
        out->set_world_poles(WorldPoles::SOUTH_POLE);
        break;
    default:
        break;
    }
    for (int yy = 0; yy < height; yy++)
        for (int xx = 0; xx < width; xx++)
        {
            df::region_map_entry * map_entry = &data->region_map[xx][yy];
            df::world_region * region = data->regions[map_entry->region_id];
            out->add_elevation(map_entry->elevation);
            out->add_rainfall(map_entry->rainfall);
            out->add_vegetation(map_entry->vegetation);
            out->add_temperature(map_entry->temperature);
            out->add_evilness(map_entry->evilness);
            out->add_drainage(map_entry->drainage);
            out->add_volcanism(map_entry->volcanism);
            out->add_savagery(map_entry->savagery);
            out->add_salinity(map_entry->salinity);
            auto clouds = out->add_clouds();
            clouds->set_cirrus(map_entry->clouds.bits.cirrus);
            clouds->set_cumulus((RemoteFortressReader::CumulusType)map_entry->clouds.bits.cumulus);
            clouds->set_fog((RemoteFortressReader::FogType)map_entry->clouds.bits.fog);
            clouds->set_front((RemoteFortressReader::FrontType)map_entry->clouds.bits.front);
            clouds->set_stratus((RemoteFortressReader::StratusType)map_entry->clouds.bits.stratus);
            if (region->type == world_region_type::Lake)
            {
                out->add_water_elevation(region->lake_surface);
            }
            else
                out->add_water_elevation(99);
        }
    DFCoord pos = GetMapCenter();
    out->set_center_x(pos.x);
    out->set_center_y(pos.y);
    out->set_center_z(pos.z);


    out->set_cur_year(World::ReadCurrentYear());
    out->set_cur_year_tick(World::ReadCurrentTick());
    return CR_OK;
}

static void AddRegionTiles(WorldMap * out, df::region_map_entry * e1, df::world_data * worldData)
{
    df::world_region * region = worldData->regions[e1->region_id];
    out->add_rainfall(e1->rainfall);
    out->add_vegetation(e1->vegetation);
    out->add_temperature(e1->temperature);
    out->add_evilness(e1->evilness);
    out->add_drainage(e1->drainage);
    out->add_volcanism(e1->volcanism);
    out->add_savagery(e1->savagery);
    out->add_salinity(e1->salinity);
    if (region->type == world_region_type::Lake)
        out->add_water_elevation(region->lake_surface);
    else
        out->add_water_elevation(99);
}

static void AddRegionTiles(RegionTile * out, df::region_map_entry * e1, df::world_data * worldData)
{
    df::world_region * region = worldData->regions[e1->region_id];
    out->set_rainfall(e1->rainfall);
    out->set_vegetation(e1->vegetation);
    out->set_temperature(e1->temperature);
    out->set_evilness(e1->evilness);
    out->set_drainage(e1->drainage);
    out->set_volcanism(e1->volcanism);
    out->set_savagery(e1->savagery);
    out->set_salinity(e1->salinity);
    if (region->type == world_region_type::Lake)
        out->set_water_elevation(region->lake_surface);
    else
        out->set_water_elevation(99);
}

static void AddRegionTiles(WorldMap * out, df::coord2d pos, df::world_data * worldData)
{
    if (pos.x < 0)
        pos.x = 0;
    if (pos.y < 0)
        pos.y = 0;
    if (pos.x >= worldData->world_width)
        pos.x = worldData->world_width - 1;
    if (pos.y >= worldData->world_height)
        pos.y = worldData->world_height - 1;
    AddRegionTiles(out, &worldData->region_map[pos.x][pos.y], worldData);
}

static void AddRegionTiles(RegionTile * out, df::coord2d pos, df::world_data * worldData)
{
    if (pos.x < 0)
        pos.x = 0;
    if (pos.y < 0)
        pos.y = 0;
    if (pos.x >= worldData->world_width)
        pos.x = worldData->world_width - 1;
    if (pos.y >= worldData->world_height)
        pos.y = worldData->world_height - 1;
    AddRegionTiles(out, &worldData->region_map[pos.x][pos.y], worldData);
}

static df::coord2d ShiftCoords(df::coord2d source, int direction)
{
    switch (direction)
    {
    case 1:
        return df::coord2d(source.x - 1, source.y + 1);
    case 2:
        return df::coord2d(source.x, source.y + 1);
    case 3:
        return df::coord2d(source.x + 1, source.y + 1);
    case 4:
        return df::coord2d(source.x - 1, source.y);
    case 5:
        return source;
    case 6:
        return df::coord2d(source.x + 1, source.y);
    case 7:
        return df::coord2d(source.x - 1, source.y - 1);
    case 8:
        return df::coord2d(source.x, source.y - 1);
    case 9:
        return df::coord2d(source.x + 1, source.y - 1);
    default:
        return source;
    }
}

static void CopyLocalMap(df::world_data * worldData, df::world_region_details* worldRegionDetails, WorldMap * out)
{
    int pos_x = worldRegionDetails->pos.x;
    int pos_y = worldRegionDetails->pos.y;
    out->set_map_x(pos_x);
    out->set_map_y(pos_y);
    out->set_world_width(17);
    out->set_world_height(17);
    char name[256];
    sprintf(name, "Region %d, %d", pos_x, pos_y);
    out->set_name_english(name);
    out->set_name(name);
    auto poles = worldData->flip_latitude;
    switch (poles)
    {
    case df::world_data::North:
        out->set_world_poles(WorldPoles::NORTH_POLE);
        break;
    case df::world_data::South:
        out->set_world_poles(WorldPoles::SOUTH_POLE);
        break;
    default:
        break;
    }

    df::world_region_details * south = NULL;
    df::world_region_details * east = NULL;
    df::world_region_details * southEast = NULL;

    for (int i = 0; i < worldData->midmap_data.region_details.size(); i++)
    {
        auto region = worldData->midmap_data.region_details[i];
        if (region->pos.x == pos_x + 1 && region->pos.y == pos_y + 1)
            southEast = region;
        else if (region->pos.x == pos_x + 1 && region->pos.y == pos_y)
            east = region;
        else if (region->pos.x == pos_x && region->pos.y == pos_y + 1)
            south = region;
    }

    for (int yy = 0; yy < 17; yy++)
        for (int xx = 0; xx < 17; xx++)
        {
            //This is because the bottom row doesn't line up.
            if (xx == 16 && yy == 16 && southEast != NULL)
            {
                out->add_elevation(southEast->elevation[0][0]);
                AddRegionTiles(out, ShiftCoords(df::coord2d(pos_x + 1, pos_y + 1), (southEast->biome[0][0])), worldData);
            }
            else if (xx == 16 && east != NULL)
            {
                out->add_elevation(east->elevation[0][yy]);
                AddRegionTiles(out, ShiftCoords(df::coord2d(pos_x + 1, pos_y), (east->biome[0][yy])), worldData);
            }
            else if (yy == 16 && south != NULL)
            {
                out->add_elevation(south->elevation[xx][0]);
                AddRegionTiles(out, ShiftCoords(df::coord2d(pos_x, pos_y + 1), (south->biome[xx][0])), worldData);
            }
            else
            {
                out->add_elevation(worldRegionDetails->elevation[xx][yy]);
                AddRegionTiles(out, ShiftCoords(df::coord2d(pos_x, pos_y), (worldRegionDetails->biome[xx][yy])), worldData);
            }

            if (xx == 16 || yy == 16)
            {
                out->add_river_tiles();
            }
            else
            {
                auto riverTile = out->add_river_tiles();
                auto east = riverTile->mutable_east();
                auto north = riverTile->mutable_north();
                auto south = riverTile->mutable_south();
                auto west = riverTile->mutable_west();

                north->set_active(worldRegionDetails->rivers_vertical.active[xx][yy]);
                north->set_elevation(worldRegionDetails->rivers_vertical.elevation[xx][yy]);
                north->set_min_pos(worldRegionDetails->rivers_vertical.x_min[xx][yy]);
                north->set_max_pos(worldRegionDetails->rivers_vertical.x_max[xx][yy]);

                south->set_active(worldRegionDetails->rivers_vertical.active[xx][yy + 1]);
                south->set_elevation(worldRegionDetails->rivers_vertical.elevation[xx][yy + 1]);
                south->set_min_pos(worldRegionDetails->rivers_vertical.x_min[xx][yy + 1]);
                south->set_max_pos(worldRegionDetails->rivers_vertical.x_max[xx][yy + 1]);

                west->set_active(worldRegionDetails->rivers_horizontal.active[xx][yy]);
                west->set_elevation(worldRegionDetails->rivers_horizontal.elevation[xx][yy]);
                west->set_min_pos(worldRegionDetails->rivers_horizontal.y_min[xx][yy]);
                west->set_max_pos(worldRegionDetails->rivers_horizontal.y_max[xx][yy]);

                east->set_active(worldRegionDetails->rivers_horizontal.active[xx + 1][yy]);
                east->set_elevation(worldRegionDetails->rivers_horizontal.elevation[xx + 1][yy]);
                east->set_min_pos(worldRegionDetails->rivers_horizontal.y_min[xx + 1][yy]);
                east->set_max_pos(worldRegionDetails->rivers_horizontal.y_max[xx + 1][yy]);
            }
        }
}

static void CopyLocalMap(df::world_data * worldData, df::world_region_details* worldRegionDetails, RegionMap * out)
{
    int pos_x = worldRegionDetails->pos.x;
    int pos_y = worldRegionDetails->pos.y;
    out->set_map_x(pos_x);
    out->set_map_y(pos_y);
    char name[256];
    sprintf(name, "Region %d, %d", pos_x, pos_y);
    out->set_name_english(name);
    out->set_name(name);


    df::world_region_details * south = NULL;
    df::world_region_details * east = NULL;
    df::world_region_details * southEast = NULL;

    for (int i = 0; i < worldData->midmap_data.region_details.size(); i++)
    {
        auto region = worldData->midmap_data.region_details[i];
        if (region->pos.x == pos_x + 1 && region->pos.y == pos_y + 1)
            southEast = region;
        else if (region->pos.x == pos_x + 1 && region->pos.y == pos_y)
            east = region;
        else if (region->pos.x == pos_x && region->pos.y == pos_y + 1)
            south = region;
    }

    for (int yy = 0; yy < 17; yy++)
        for (int xx = 0; xx < 17; xx++)
        {
            auto tile = out->add_tiles();
            //This is because the bottom row doesn't line up.
            if (xx == 16 && yy == 16 && southEast != NULL)
            {
                tile->set_elevation(southEast->elevation[0][0]);
                AddRegionTiles(tile, ShiftCoords(df::coord2d(pos_x + 1, pos_y + 1), (southEast->biome[0][0])), worldData);
            }
            else if (xx == 16 && east != NULL)
            {
                tile->set_elevation(east->elevation[0][yy]);
                AddRegionTiles(tile, ShiftCoords(df::coord2d(pos_x + 1, pos_y), (east->biome[0][yy])), worldData);
            }
            else if (yy == 16 && south != NULL)
            {
                tile->set_elevation(south->elevation[xx][0]);
                AddRegionTiles(tile, ShiftCoords(df::coord2d(pos_x, pos_y + 1), (south->biome[xx][0])), worldData);
            }
            else
            {
                tile->set_elevation(worldRegionDetails->elevation[xx][yy]);
                AddRegionTiles(tile, ShiftCoords(df::coord2d(pos_x, pos_y), (worldRegionDetails->biome[xx][yy])), worldData);
            }

            auto riverTile = tile->mutable_river_tiles();
            auto east = riverTile->mutable_east();
            auto north = riverTile->mutable_north();
            auto south = riverTile->mutable_south();
            auto west = riverTile->mutable_west();

            north->set_active(worldRegionDetails->rivers_vertical.active[xx][yy]);
            north->set_elevation(worldRegionDetails->rivers_vertical.elevation[xx][yy]);
            north->set_min_pos(worldRegionDetails->rivers_vertical.x_min[xx][yy]);
            north->set_max_pos(worldRegionDetails->rivers_vertical.x_max[xx][yy]);

            south->set_active(worldRegionDetails->rivers_vertical.active[xx][yy + 1]);
            south->set_elevation(worldRegionDetails->rivers_vertical.elevation[xx][yy + 1]);
            south->set_min_pos(worldRegionDetails->rivers_vertical.x_min[xx][yy + 1]);
            south->set_max_pos(worldRegionDetails->rivers_vertical.x_max[xx][yy + 1]);

            west->set_active(worldRegionDetails->rivers_horizontal.active[xx][yy]);
            west->set_elevation(worldRegionDetails->rivers_horizontal.elevation[xx][yy]);
            west->set_min_pos(worldRegionDetails->rivers_horizontal.y_min[xx][yy]);
            west->set_max_pos(worldRegionDetails->rivers_horizontal.y_max[xx][yy]);

            east->set_active(worldRegionDetails->rivers_horizontal.active[xx + 1][yy]);
            east->set_elevation(worldRegionDetails->rivers_horizontal.elevation[xx + 1][yy]);
            east->set_min_pos(worldRegionDetails->rivers_horizontal.y_min[xx + 1][yy]);
            east->set_max_pos(worldRegionDetails->rivers_horizontal.y_max[xx + 1][yy]);
        }
}

static command_result GetRegionMaps(color_ostream &stream, const EmptyMessage *in, RegionMaps *out)
{
    if (!df::global::world->world_data)
    {
        return CR_FAILURE;
    }
    df::world_data * data = df::global::world->world_data;
    for (int i = 0; i < data->midmap_data.region_details.size(); i++)
    {
        df::world_region_details * region = data->midmap_data.region_details[i];
        if (!region)
            continue;
        WorldMap * regionMap = out->add_world_maps();
        CopyLocalMap(data, region, regionMap);
    }
    return CR_OK;
}

static command_result GetRegionMapsNew(color_ostream &stream, const EmptyMessage *in, RegionMaps *out)
{
    if (!df::global::world->world_data)
    {
        return CR_FAILURE;
    }
    df::world_data * data = df::global::world->world_data;
    for (int i = 0; i < data->midmap_data.region_details.size(); i++)
    {
        df::world_region_details * region = data->midmap_data.region_details[i];
        if (!region)
            continue;
        WorldMap * regionMap = out->add_world_maps();
        CopyLocalMap(data, region, regionMap);
    }
    return CR_OK;
}

static command_result GetCreatureRaws(color_ostream &stream, const EmptyMessage *in, CreatureRawList *out)
{
    if (!df::global::world)
        return CR_FAILURE;

    df::world * world = df::global::world;

    for (int i = 0; i < world->raws.creatures.all.size(); i++)
    {
        df::creature_raw * orig_creature = world->raws.creatures.all[i];

        auto send_creature = out->add_creature_raws();

        send_creature->set_index(i);
        send_creature->set_creature_id(orig_creature->creature_id);
        send_creature->add_name(orig_creature->name[0]);
        send_creature->add_name(orig_creature->name[1]);
        send_creature->add_name(orig_creature->name[2]);

        send_creature->add_general_baby_name(orig_creature->general_baby_name[0]);
        send_creature->add_general_baby_name(orig_creature->general_baby_name[1]);

        send_creature->add_general_child_name(orig_creature->general_child_name[0]);
        send_creature->add_general_child_name(orig_creature->general_child_name[1]);

        send_creature->set_creature_tile(orig_creature->creature_tile);
        send_creature->set_creature_soldier_tile(orig_creature->creature_soldier_tile);

        ConvertDfColor(orig_creature->color, send_creature->mutable_color());

        send_creature->set_adultsize(orig_creature->adultsize);

        for (int j = 0; j < orig_creature->caste.size(); j++)
        {
            auto orig_caste = orig_creature->caste[j];
            if (!orig_caste)
                continue;
            auto send_caste = send_creature->add_caste();

            send_caste->set_index(j);

            send_caste->set_caste_id(orig_caste->caste_id);

            send_caste->add_caste_name(orig_caste->caste_name[0]);
            send_caste->add_caste_name(orig_caste->caste_name[1]);
            send_caste->add_caste_name(orig_caste->caste_name[2]);

            send_caste->add_baby_name(orig_caste->baby_name[0]);
            send_caste->add_baby_name(orig_caste->baby_name[1]);

            send_caste->add_child_name(orig_caste->child_name[0]);
            send_caste->add_child_name(orig_caste->child_name[1]);
            send_caste->set_gender(orig_caste->sex);

            for (int partIndex = 0; partIndex < orig_caste->body_info.body_parts.size(); partIndex++)
            {
                auto orig_part = orig_caste->body_info.body_parts[partIndex];
                if (!orig_part)
                    continue;
                auto send_part = send_caste->add_body_parts();
                
                send_part->set_token(orig_part->token);
                send_part->set_category(orig_part->category);
                send_part->set_parent(orig_part->con_part_id);

                for (int partFlagIndex = 0; partFlagIndex <= ENUM_LAST_ITEM(body_part_raw_flags); partFlagIndex++)
                {
                    send_part->add_flags(orig_part->flags.is_set((body_part_raw_flags::body_part_raw_flags)partFlagIndex));
                }

                for (int layerIndex = 0; layerIndex < orig_part->layers.size(); layerIndex++)
                {
                    auto orig_layer = orig_part->layers[layerIndex];
                    if (!orig_layer)
                        continue;
                    auto send_layer = send_part->add_layers();

                    send_layer->set_layer_name(orig_layer->layer_name);
                    send_layer->set_tissue_id(orig_layer->tissue_id);
                    send_layer->set_layer_depth(orig_layer->layer_depth);
                    for (int layerModIndex = 0; layerModIndex < orig_layer->bp_modifiers.size(); layerModIndex++)
                    {
                        send_layer->add_bp_modifiers(orig_layer->bp_modifiers[layerModIndex]);
                    }
                }

                send_part->set_relsize(orig_part->relsize);
            }

            send_caste->set_total_relsize(orig_caste->body_info.total_relsize);

            for (int k = 0; k < orig_caste->bp_appearance.modifiers.size(); k++)
            {
                auto send_mod = send_caste->add_modifiers();
                auto orig_mod = orig_caste->bp_appearance.modifiers[k];
                send_mod->set_type(ENUM_KEY_STR(appearance_modifier_type, orig_mod->modifier.type));

                if (orig_mod->modifier.growth_rate > 0)
                {
                    send_mod->set_mod_min(orig_mod->modifier.growth_min);
                    send_mod->set_mod_max(orig_mod->modifier.growth_max);
                }
                else
                {
                    send_mod->set_mod_min(orig_mod->modifier.ranges[0]);
                    send_mod->set_mod_max(orig_mod->modifier.ranges[6]);
                }

            }
            for (int k = 0; k < orig_caste->bp_appearance.modifier_idx.size(); k++)
            {
                send_caste->add_modifier_idx(orig_caste->bp_appearance.modifier_idx[k]);
                send_caste->add_part_idx(orig_caste->bp_appearance.part_idx[k]);
                send_caste->add_layer_idx(orig_caste->bp_appearance.layer_idx[k]);
            }
            for (int k = 0; k < orig_caste->body_appearance_modifiers.size(); k++)
            {
                auto send_mod = send_caste->add_body_appearance_modifiers();
                auto orig_mod = orig_caste->body_appearance_modifiers[k];

                send_mod->set_type(ENUM_KEY_STR(appearance_modifier_type, orig_mod->modifier.type));

                if (orig_mod->modifier.growth_rate > 0)
                {
                    send_mod->set_mod_min(orig_mod->modifier.growth_min);
                    send_mod->set_mod_max(orig_mod->modifier.growth_max);
                }
                else
                {
                    send_mod->set_mod_min(orig_mod->modifier.ranges[0]);
                    send_mod->set_mod_max(orig_mod->modifier.ranges[6]);
                }
            }
            for (int k = 0; k < orig_caste->color_modifiers.size(); k++)
            {
                auto send_mod = send_caste->add_color_modifiers();
                auto orig_mod = orig_caste->color_modifiers[k];

                for (int l = 0; l < orig_mod->pattern_index.size(); l++)
                {
                    auto orig_pattern = world->raws.descriptors.patterns[orig_mod->pattern_index[l]];
                    auto send_pattern = send_mod->add_patterns();

                    for (int m = 0; m < orig_pattern->colors.size(); m++)
                    {
                        auto send_color = send_pattern->add_colors();
                        auto orig_color = world->raws.descriptors.colors[orig_pattern->colors[m]];
                        send_color->set_red(orig_color->red * 255.0);
                        send_color->set_green(orig_color->green * 255.0);
                        send_color->set_blue(orig_color->blue * 255.0);
                    }

                    send_pattern->set_id(orig_pattern->id);
                    send_pattern->set_pattern((PatternType)orig_pattern->pattern);
                }

                for (int l = 0; l < orig_mod->body_part_id.size(); l++)
                {
                    send_mod->add_body_part_id(orig_mod->body_part_id[l]);
                    send_mod->add_tissue_layer_id(orig_mod->tissue_layer_id[l]);
                    send_mod->set_start_date(orig_mod->start_date);
                    send_mod->set_end_date(orig_mod->end_date);
                    send_mod->set_part(orig_mod->part);
                }
            }

            send_caste->set_description(orig_caste->description);
            send_caste->set_adult_size(orig_caste->misc.adult_size);
        }
    
        for (int j = 0; j < orig_creature->tissue.size(); j++)
        {
            auto orig_tissue = orig_creature->tissue[j];
            auto send_tissue = send_creature->add_tissues();

            send_tissue->set_id(orig_tissue->id);
            send_tissue->set_name(orig_tissue->tissue_name_singular);
            send_tissue->set_subordinate_to_tissue(orig_tissue->subordinate_to_tissue);

            auto send_mat = send_tissue->mutable_material();

            send_mat->set_mat_index(orig_tissue->mat_index);
            send_mat->set_mat_type(orig_tissue->mat_type);
        }
}

    return CR_OK;
}

static command_result GetPlantRaws(color_ostream &stream, const EmptyMessage *in, PlantRawList *out)
{
    if (!df::global::world)
        return CR_FAILURE;

    df::world * world = df::global::world;

    for (int i = 0; i < world->raws.plants.all.size(); i++)
    {
        df::plant_raw* plant_local = world->raws.plants.all[i];
        PlantRaw* plant_remote = out->add_plant_raws();

        plant_remote->set_index(i);
        plant_remote->set_id(plant_local->id);
        plant_remote->set_name(plant_local->name);
        if (!plant_local->flags.is_set(df::plant_raw_flags::TREE))
            plant_remote->set_tile(plant_local->tiles.shrub_tile);
        else
            plant_remote->set_tile(plant_local->tiles.tree_tile);
    }
    return CR_OK;
}

static command_result CopyScreen(color_ostream &stream, const EmptyMessage *in, ScreenCapture *out)
{
    df::graphicst * gps = df::global::gps;
    out->set_width(gps->dimx);
    out->set_height(gps->dimy);
    for (int i = 0; i < (gps->dimx * gps->dimy); i++)
    {
        int index = i * 4;
        auto tile = out->add_tiles();
        tile->set_character(gps->screen[index]);
        tile->set_foreground(gps->screen[index + 1] | (gps->screen[index + 3] * 8));
        tile->set_background(gps->screen[index + 2]);
    }

    return CR_OK;
}

static command_result PassKeyboardEvent(color_ostream &stream, const KeyboardEvent *in)
{
    SDL::Event e;
    e.key.type = in->type();
    e.key.state = in->state();
    e.key.ksym.mod = (SDL::Mod)in->mod();
    e.key.ksym.scancode = in->scancode();
    e.key.ksym.sym = (SDL::Key)in->sym();
    e.key.ksym.unicode = in->unicode();
    SDL_PushEvent(&e);
    return CR_OK;
}