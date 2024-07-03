-- Adds grasses to pre-0.31.19 worlds, once the raws have been injected

local cave_grasses = {}
for idx,pl in ipairs(df.global.world.raws.plants.grasses) do
    if pl.flags.BIOME_SUBTERRANEAN_WATER or pl.flags.BIOME_SUBTERRANEAN_CHASM then
        table.insert(cave_grasses,df.global.world.raws.plants.grasses_idx[idx])
    end
end

local layers = 0
for _,fl in ipairs(df.global.world.world_data.underground_regions) do
    if fl.type == df.feature_layer_type.SUBTERRANEAN and fl.water >= 10 then
        for _,id in ipairs(cave_grasses) do
            fl.feature_init.feature.population:insert('#', {
                new = true,
                type = df.world_population_type.Grass,
                plant = id
            })
        end
        local wt = df.global.world.area_grasses.world_tiles
        local gr = df.global.world.area_grasses.layer_grasses
        for i=0,#wt.x-1 do
            local found = false
            for j=0,#fl.region_coords.x-1 do
                if wt.x[i] == fl.region_coords.x[j] and wt.y[i] == fl.region_coords.y[j] and not found then
                   found = true
                   local g = #gr
                   gr:insert('#', {
                       new = true
                   })
                   gr[g].ref.region_x = wt.x[i]
                   gr[g].ref.region_y = wt.y[i]
                   gr[g].ref.cave_id = fl.index
                   gr[g].ref.population_idx = -1
                   gr[g].ref.depth = fl.layer_depth
                   for _,k in ipairs(cave_grasses) do
                       gr[g].grasses:insert('#', k)
                   end
                end
            end
        end
        layers = layers + 1
    end
end
if layers > 0 then
    print("Grassified "..layers.." cavern layers")
end

local regions = 0
for _,sr in ipairs(df.global.world.world_data.regions) do
    local grasses = {}
    for idx,pl in ipairs(df.global.world.raws.plants.grasses) do
        local found = false
        for b,bn in ipairs(df.biome_type) do
            if sr.biome_tile_counts[b] > 0 and pl.flags["BIOME_"..bn] then
                found = true
            end
        end
        if found then
            if pl.flags.GOOD and not sr.good then
                found = false
            end
            if pl.flags.EVIL and not sr.evil then
                found = false
            end
        end
        if found then
            sr.population:insert('#', {
                new = true,
                type = df.world_population_type.Grass,
                plant = df.global.world.raws.plants.grasses_idx[idx]
            })
            table.insert(grasses, df.global.world.raws.plants.grasses_idx[idx])
        end
    end
    if #grasses > 0 then
        local wt = df.global.world.area_grasses.world_tiles
        local gr = df.global.world.area_grasses.layer_grasses
        for i=0,#wt.x-1 do
            local found = false
            for j=0,#sr.region_coords.x-1 do
                if wt.x[i] == sr.region_coords.x[j] and wt.y[i] == sr.region_coords.y[j] and not found then
                   found = true
                   local g = #gr
                   gr:insert('#', {
                       new = true
                   })
                   gr[g].ref.region_x = wt.x[i]
                   gr[g].ref.region_y = wt.y[i]
                   gr[g].ref.population_idx = -1
                   gr[g].ref.depth = -1
                   for _,k in ipairs(grasses) do
                       gr[g].grasses:insert('#', k)
                   end
                end
            end
        end
        regions = regions + 1
    end
end
if regions > 0 then
    print("Grassified "..regions.." subregions")
end

