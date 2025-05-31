#include "df/building_animaltrapst.h"
#include "df/job.h"

using namespace df::enums;

struct animaltrap_lessbait_hook : df::building_animaltrapst {
    typedef df::building_animaltrapst interpose_base;
    DEFINE_VMETHOD_INTERPOSE(void, updateAction, ())
    {
        // Run the original logic first
        INTERPOSE_NEXT(updateAction)();

        // Check if we need to fix any Bait jobs
        if ((getBuildStage() != getMaxBuildStage()) || (contained_items.size() != 1))
            return;

        df::job *bait_job = NULL;

        // Check if we've got a "Bait Trap" job
        for (size_t j = 0; j < jobs.size(); j++)
            if (jobs[j]->job_type == df::job_type::BaitTrap)
                bait_job = jobs[j];
        if (!bait_job)
            return;

        // Check which bait item the job claimed, if any
        for (size_t i = 0; i < bait_job->items.size(); i++)
        {
            auto it = bait_job->items[i]->item;
            // Ignore items of the wrong type
            if ((it == NULL) || (it->getType() != bait_job->item_type))
                continue;

            // If the stack size is greater than 1, then split all but 1
            // into a separate item stack in the same location
            // (so that the stack of 1 remains attached to the job)
            auto stack = it->getStackSize();
            if (stack > 1)
                it->splitStack(stack - 1, true)->categorize(true);
            return;
        }
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(animaltrap_lessbait_hook, updateAction);
