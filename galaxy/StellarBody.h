#ifndef _STELLAR_BODY_H_
#define _STELLAR_BODY_H_

#include "type_vec2.h"
#include <vector>
#include <string>

struct Population{
    int amount = 0;
    float base_growth = 1.01; //The amount of growth
    //Modifiers
    bool food_shortage = false;
    bool water_shortage = false;
    float food_decline = 0.95;
    float water_decline = 0.9;
};

typedef enum {
    RESOURCE_INVALID = -1,
    RESOURCE_WATER = 0,
    RESOURCE_FOOD = 1,
    RESOURCE_MEDICINE = 2,
    RESOURCE_METALS = 3
}resource_id;


struct Resource{
    int type = -1;  //Reference resource_types
    int weight = 1;
    int aquired_price = 0;
};

//Can hold any unique resource.
struct ResourceSlot{
    Resource resource;
    int amount;
    bool AddResource(Resource& r, int amount);
    bool IncrementResource(int count);
};

int             GetTotalPopulation(Population& population);
int             GetTotalResources(std::vector<ResourceSlot>& resource_slots, int type);
void            PopulationProgress(Population& population,std::vector<ResourceSlot>& resource_slots);

bool            AddResourceToSlots(std::vector<ResourceSlot>& resource_slots, ResourceSlot slot);
ResourceSlot*   FindResourceInSlots(std::vector<ResourceSlot>& resource_slots, int id);
bool            ConsumeResource(Resource* resource, int amount);
const char*     ResourceNameByType(int type);
int             ResourceBasePriceByType(int type);

//A structure produces and consumes from slots at this rate.
class Structure{
    public:
    std::string name;
    std::vector<ResourceSlot>productionrate_slots;
    std::vector<ResourceSlot>consumptionrate_slots;
    void Progress(std::vector<ResourceSlot>& production_slots,std::vector<ResourceSlot>& consumption_slots);


};

class Ship{
    public:

    Population population;
    std::vector<ResourceSlot>resource_slots;
};

class Contract{
public:
    ResourceSlot resource_slot;
    int delivery_time = 1;
    bool buy = true; //If not, it's a sell contract.
    float markup = 1.0; //Price compared to base price.
    bool fulfilled = false;
    void UpdateContract();
};

//Colony
class Colony{
    public:

    int credits = 1000;

    Population population;
    std::vector<ResourceSlot>resource_slots;
    std::vector<Structure>structures;

    //When we run out of a resource that we need,
    //we need to request that it get delivered via a contract.
    std::vector<Contract>contracts;

    //Statistics?
    int food_consumed = 0;
    int food_gained = 0;
};

//Stellar bodies can be a sun, or a planet, or a space station.

typedef enum {
    BODY_INVALID = -1,
    BODY_STAR = 0,
    BODY_PLANET = 1,
    BODY_ASTEROID = 2
}stellarbody_type;

class StellarBody;
class StellarBody{
public:
    StellarBody();
    ~StellarBody();

    stellarbody_type type;

    vec2 coordinate;
    float likelyhood = 0; //How big was the chance it spawned?
};


#endif