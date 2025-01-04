#ifndef _STELLAR_BODY_H_
#define _STELLAR_BODY_H_

#include "type_vec2.h"
#include <vector>
#include <string>
#include "Object.h"
#include "AssetManager.h"
#include "Bezier2D.h"

struct Population{
    int amount = 0;
    float base_growth = 1.01; //The amount of growth
    //Modifiers
    bool food_shortage = false;
    bool water_shortage = false;
    float food_decline = 0.75;
    float water_decline = 0.9;
};

typedef enum {
    RESOURCE_INVALID = -1,
    RESOURCE_WATER = 0,
    RESOURCE_FOOD = 1,
    RESOURCE_MEDICINE = 2,
    RESOURCE_METAL = 3
}resource_id;


struct Resource{
    int type = RESOURCE_INVALID;  //Reference resource_types
    int weight = 1;
    int aquired_price = 0;
};

//Can hold any unique resource.
struct ResourceSlot{
    Resource resource;
    int amount = 0;
    bool AddResource(Resource& r, int amount);
    bool IncrementResource(int count);
    int TakeResourceAmount(int amount);
    ResourceSlot TakeResource(int count);
};

int             GetTotalPopulation(Population& population);
int             GetTotalResources(std::vector<ResourceSlot>& resource_slots, int type);

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

typedef enum {
    VOID_CONTRACT = -1,
    BUY_CONTRACT = 0,
    SELL_CONTRACT = 1
}contract_type_id;

class Colony;
class Contract{
public:
    ResourceSlot offer;
    int delivery_time = 1;
    int contract_type = VOID_CONTRACT;
    float markup = 1.0; //Price compared to base price.
    bool fulfilled = false;
    //Target market where the contract should be fulfilled.
    Colony* target = NULL;

    void UpdateContract();
    void Fulfill(ResourceSlot* source);
};


//Stuff that it overproduced will be put on the market at an increasingly cheap price.
//It will be moved out of colony storage and put in market storage.
//Rates can be determined like 100, 1000, 10k etc.
class Market;
class Market{
    public:
    std::vector<ResourceSlot>sell_slots;  // The resources that it has in storage and wants to sell off.
    std::vector<ResourceSlot>buy_slots;   // The resources that we want to buy, but don't have.

    ResourceSlot BuyFromMarket(int resource_type,int amount);
    int SellToMarket(ResourceSlot* resource_slot, int amount);
};

//Colony
class Colony{
    public:
    std::string name;
    int credits = 1000;

    Population population;
    std::vector<ResourceSlot>resource_slots;
    std::vector<Structure>structures;
    Market* market = NULL;

    //When we run out of a resource that we need,
    //we need to request that it get delivered via a contract.
    std::vector<Contract>contracts;

    //Statistics/settings?
    int food_reserves = 1000;  //Amount of food packets that are in reserve to feed the population
    int food_consumed = 0;
    int food_gained = 0;

    void Progress();
    Contract GetContract(int resource_type, int amount, int contract_type);
};



//Stellar bodies can be a sun, or a planet, or a space station.
typedef enum {
    BODY_INVALID = -1,
    BODY_STAR = 0,
    BODY_PLANET = 1,
    BODY_ASTEROID = 2,
    BODY_SHIP = 3
}stellarbody_type;

class StellarBody;
class Route;

class Route{
    public:
    Route(){};
    Route(Route* route){
        start = route->start;
        end = route->end;
    };
    ~Route(){};

    void Setup(StellarBody* start, StellarBody* end);
    void Reverse();

    StellarBody* start = NULL;
    StellarBody* end = NULL;

    float GetDistance();
};

//Storage decoupled from object, so it can be run in simulation.
//Ships will follow routes and get delayed or get off track randomly.
//This would then update the object when you view them.
//When viewing and interacting, object manipulates this data.

class StellarBody {
public:
    StellarBody();
    ~StellarBody();

    stellarbody_type type;

    vec2 coordinate;
    vec2 heading = vec2(0,1);
    float likelyhood = 0;   // How big was the chance it spawned?

    Colony* colony = NULL;  // Some may have a single colony.

    Route* route = NULL;    // A route it should be able to follow.

    bool f_updatevisual = false;    // When data is modified and visuals need updating.

    void MoveForward(float delta);
    void Turn(float delta);

    void UpdateRouteInfo();
    void PlaceOnRoute(Route* route);
    void FollowRoute();
    void PickupResource(ResourceSlot& order, StellarBody* target);
    StellarBody* FindClosest(std::vector<StellarBody*>&list, int type); //Find closest body to this one from a list.
};

class RouteObject;
class StellarObject : public Object{
public:
    StellarObject(){};
    StellarObject(StellarBody* body);
    ~StellarObject(){};

    StellarBody* stellarbody = NULL;
    void UpdatePosition();
    void PlaceOnRoute(RouteObject* route);

    static StellarObject* CreateNewStar(AssetManager* assetmanager);
    static StellarObject* CreateNewShip(AssetManager* assetmanager);
};

//We build a route for a ship to follow. Uses a 2D spline limited to a linear one for now.
class RouteObject : public Object{
public:
    RouteObject(){};
    ~RouteObject(){};

    Route* route = NULL;
    Bezier2D curve = Bezier2D(2);

    void SetupNewRoute(StellarObject* from, StellarObject* to, AssetManager* assetmanager);
    void UpdateRoute();
};

#endif

/*
    Colonies generate and consume food.
    When there is overproduction, the food supply increases and above a certain threshold
    these should be placed on the open market for an increasingly cheap price.

    The ships need to have some incentive to go do something, which is transport food mainly.
    - [x] The food consumption rate should have factions by taking a single item from supply, and tracking it's fraction.

    Objectives:
    - [ ] Buy item at Market A and transport to Market B.
    - [ ] Goto Market A and stay docked.
    - [ ] Transport People from A to B. (Which may as well be items...)
*/