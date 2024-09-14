#include "df/building_doorst.h"
#include "df/building_drawbuffer.h"
#include "df/building_floodgatest.h"

using namespace df::enums;

struct door_tile_hook : df::building_doorst {
    typedef df::building_doorst interpose_base;
    DEFINE_VMETHOD_INTERPOSE(void, drawBuilding, (df::building_drawbuffer* db, int16_t unk))
    {
        INTERPOSE_NEXT(drawBuilding)(db, unk);
        if (getBuildStage() == 1 && mat_type == 0)
        {
            MaterialInfo mat(mat_type, mat_index);
            if (mat.material->flags.is_set(df::material_flags::IS_METAL))
                db->tile[0][0] = 0xD8;
        }
    }
};

struct floodgate_tile_hook : df::building_floodgatest {
    typedef df::building_floodgatest interpose_base;
    DEFINE_VMETHOD_INTERPOSE(void, drawBuilding, (df::building_drawbuffer* db, int16_t unk))
    {
        INTERPOSE_NEXT(drawBuilding)(db, unk);
        if (getBuildStage() == 1 && mat_type == 0)
        {
            MaterialInfo mat(mat_type, mat_index);
            if (mat.material->flags.is_set(df::material_flags::IS_METAL))
                db->tile[0][0] = 0xF0;
        }
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(door_tile_hook, drawBuilding);
IMPLEMENT_VMETHOD_INTERPOSE(floodgate_tile_hook, drawBuilding);
