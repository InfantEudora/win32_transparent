#include "StellarBody.h"

#include "Debug.h"
static Debugger *debug = new Debugger("Population", DEBUG_ALL);

std::vector<std::string>resource_types = {
    "Water",
    "Food",
    "Medicine",
    "Metals"
};

std::vector<int>resource_base_prices = {
    10,
    30,
    80,
    50
};

StellarBody::StellarBody(){

}

StellarBody::~StellarBody(){

}

const char* ResourceNameByType(int type){
    if (type <= RESOURCE_INVALID){
        return "Invalid";
    }
    if (type < resource_types.size()){
        return resource_types.at(type).c_str();
    }
    return "Invalid";
}

int ResourceBasePriceByType(int type){
    if (type <= RESOURCE_INVALID){
        return 0;
    }
    if (type < resource_types.size()){
        return resource_base_prices.at(type);
    }
    return 0;
}

bool ResourceSlot::IncrementResource(int count){
    if (resource.type == RESOURCE_INVALID){
        return false;
    }
    amount += count;
    return true;
}

bool ResourceSlot::AddResource(Resource& r, int count){
    //Initial placement
    if (resource.type == RESOURCE_INVALID){
        resource = r;
        amount = count;
    }else if (resource.type == r.type){
        amount += count;
    }
    return true;
}

bool AddResourceToSlots(std::vector<ResourceSlot>& resource_slots, ResourceSlot slot){
    ResourceSlot* same = FindResourceInSlots(resource_slots,slot.resource.type);
    if (same){
        same->AddResource(slot.resource,slot.amount);
        debug->Info("Stacked resource %s in existing slot\n",ResourceNameByType(same->resource.type));
        return true;
    }
    //Add to slots
    resource_slots.push_back(slot);
    debug->Info("Added new resouce slot\n");
    return true;
}


ResourceSlot* FindResourceInSlots(std::vector<ResourceSlot>& resource_slots, int type){
    for (ResourceSlot& slot:resource_slots){
        if (slot.resource.type == type){
            return &slot;
        }
    }
    return NULL;
}

//
bool ConsumeResource(ResourceSlot* slot, int amount){
    if (!slot){
        debug->Err("Cannot consume NULL resource\n");
        return false;
    }
    if (slot->resource.type == RESOURCE_INVALID){
        debug->Err("Cannot consume Invalid Resource\n");
        return false;
    }
    if (slot->amount > amount){
        slot->amount -= amount;
        debug->Info("Consumed %i %s resouce\n",amount,resource_types.at(slot->resource.type).c_str());
        return true;
    }
    debug->Warn("Consumed all remaining (%i) %s resouces\n",slot->amount,resource_types.at(slot->resource.type).c_str());
    slot->amount = 0;
    return false;
}

// Function that will consume resources and grow population. Each tick should be a month or so.
//
void PopulationProgress(Population& population,std::vector<ResourceSlot>& resource_slots){
    if (population.amount < 1){
        debug->Warn("No population left!\n");
        return;
    }

    bool growth_allowed = true;

    ResourceSlot* foodslot = FindResourceInSlots(resource_slots,RESOURCE_FOOD);
    if (foodslot){
        float consumption = (population.amount / 1000) + 1;
        consumption = roundf(consumption);
        if (!ConsumeResource(foodslot,consumption)){
            debug->Warn("No more food to consume!\n");
            growth_allowed = false;
            population.food_shortage = true;
        }
    }else{
        debug->Warn("No food found in any resouce slots!\n");
        growth_allowed = false;
        population.food_shortage = true;
    }

    if (growth_allowed){
        //Growth rate is allowed to be negative.
        float rate = (population.base_growth - 1);
        int gain = roundf((float)population.amount * rate);
        debug->Info("Population %i Gained + %i at %.2f%% population growth\n",population.amount,gain,rate*100);
        if (gain > 0){
            population.amount += gain;
        }else if (population.amount > abs(gain)){

            population.amount += gain;
        }else{
            debug->Warn("All population died\n");
            population.amount  = 0;
        }
        return;
    }

    //Decrement
    if (population.food_shortage){
        float rate = (population.food_decline - 1);
        int decline = abs(ceilf((float)population.amount * rate));
        debug->Info("Population %i : %i Died due to food shortage at %.2f%% rate\n",population.amount,decline,rate*100);
        if (population.amount > decline){
            population.amount -= decline;
        }else{
            debug->Warn("All population died\n");
            population.amount  = 0;
        }
    }
}

void Structure::Progress(std::vector<ResourceSlot>& production_slots,std::vector<ResourceSlot>& consumption_slots){
    //Production
    for (ResourceSlot& prodrateslot:productionrate_slots){
        AddResourceToSlots(production_slots,prodrateslot);
    }
}

void Contract::UpdateContract(){
    if (delivery_time > 0){
        delivery_time--;
        if (delivery_time == 0){
            debug->Info("Contract was voided.\n");
        }
    }
}