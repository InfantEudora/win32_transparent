#include "StellarBody.h"
#include <string>

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

StellarObject::StellarObject(StellarBody* body){
    stellarbody = body;
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

int ResourceSlot::TakeResource(int count){
    if (resource.type == RESOURCE_INVALID){
        return 0;
    }
    if (amount <= count){
        count = amount;
    }
    amount-= count;
    return count;
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
        //debug->Info("Stacked resource %s in existing slot\n",ResourceNameByType(same->resource.type));
        return true;
    }
    //Add to slots
    resource_slots.push_back(slot);
    //debug->Info("Added new resouce slot\n");
    return true;
}

//Returns a pointer to the slot or null
ResourceSlot* FindResourceInSlots(std::vector<ResourceSlot>& resource_slots, int type){
    for (ResourceSlot& slot:resource_slots){
        if (slot.resource.type == type){
            return &slot;
        }
    }
    return NULL;
}



void Colony::Progress(){
    if (population.amount < 1){
        //debug->Warn("No population left!\n");
        return;
    }

    int unfed_people = population.amount;
    int shortage = 0;
    int foodfromsupplies = 0;
    int foodreserve_target = population.amount + 1000;
    if (food_reserves > unfed_people){
        debug->Info("Population of %i consumed %i food from reserves\n",population.amount,unfed_people);
        //We just consume from the reserves:
        food_reserves -= unfed_people;
        unfed_people = 0;
    }else{
        debug->Info("Population of %i consumed %i remaining food reserves\n",population.amount,food_reserves);
        //We consume all reserves, and leave some consumption remaining.
        unfed_people -= food_reserves;
        food_reserves = 0;
    }

    //Consumption remaining is the amount of unfed people.
    int foodpackets = (foodreserve_target + unfed_people - food_reserves) / 1000;
    debug->Info("Open %i food packets to replenish reserves.\n",foodpackets);
    ResourceSlot* foodslot = FindResourceInSlots(resource_slots,RESOURCE_FOOD);
    if (foodslot){
        if (foodpackets > 0){
            int taken = foodslot->TakeResource(foodpackets);
            food_reserves += 1000 * taken;
            debug->Info(" Opened %i/%i food packets to replenish reserves.\n",taken,foodpackets);
        }
    }

    if (food_reserves > unfed_people){
        //Remaining pop consumes from reserves.
        food_reserves -= unfed_people;
        unfed_people = 0;
    }else{
        //We consume all reserves, and leave some consumption remaining.
        unfed_people -= food_reserves;
        food_reserves = 0;
    }

    //Population is allowed to grow
    if (unfed_people == 0){
        //Growth rate is allowed to be negative.
        population.food_shortage = false;
        float rate = (population.base_growth - 1);
        int gain = roundf((float)population.amount * rate);
        //debug->Info("Population %i Gained + %i at %.2f%% population growth\n",population.amount,gain,rate*100);
        if (gain > 0){
            population.amount += gain;
        }else if (population.amount > abs(gain)){

            population.amount += gain;
        }else{
            debug->Warn("All population died\n");
            population.amount  = 0;
        }
    }else{
        //Decrement
        population.food_shortage = true;
        int decline = (unfed_people * population.food_decline) + 1; //+ One because someone always nibbles on someone's leg.
        debug->Warn("Population %i : %i was left unfed and %i died.\n",population.amount,unfed_people,decline);
        if (population.amount > decline){
            population.amount -= decline;
        }else{
            //debug->Warn("All population died\n");
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

//Make one agains base price.
Contract Colony::GetContract(int resource_type, int amount, int contract_type){
    Contract contract;
    contract.target = this;
    contract.contract_type = contract_type;
    contract.offer.amount = amount;
    contract.offer.resource.type = resource_type;

    return contract;
}

void Contract::UpdateContract(){
    if (delivery_time > 0){
        delivery_time--;
        if (delivery_time == 0){
            debug->Info("Contract was voided.\n");
        }
    }
}

//Does the transaction from/to the specified slot
void Contract::Fulfill(ResourceSlot* slot){
    AddResourceToSlots(target->resource_slots,offer);
    fulfilled = true;
}

//Should be based on some properties of the thing they are on. Planet... star, station asteroid?
Colony* GenerateNewStarColony(){
    //For now, each colony gets to have a farm.
    Colony* colony = new Colony();

    //Setup initial conditions
    Resource food = {
        .type = RESOURCE_FOOD
    };
    ResourceSlot foodslot = {
        .resource = food,
        .amount = 10
    };
    AddResourceToSlots(colony->resource_slots,foodslot);
    colony->population.amount = 1000;

    Structure farm;
    farm.name = "Farm";
    foodslot.amount = 2;
    AddResourceToSlots(farm.productionrate_slots,foodslot);
    colony->structures.push_back(farm);

    Contract contract;
    contract.offer.AddResource(food,5);
    contract.delivery_time = 10;
    contract.markup = 1.1;
    colony->contracts.push_back(contract);
    return colony;
}

Colony* GenerateNewShipColony(){
    //For now, each colony gets to have a farm.
    Colony* colony = new Colony();

    //Setup initial conditions
    Resource food = {
        .type = RESOURCE_FOOD
    };
    ResourceSlot foodslot = {
        .resource = food,
        .amount = 100
    };
    AddResourceToSlots(colony->resource_slots,foodslot);
    colony->population.amount = 10;

    return colony;
}

StellarObject* StellarObject::CreateNewStar(AssetManager* assetmanager){
    if (!assetmanager){
        debug->Fatal("StellarObject::CreateNewStar with no assetmanager\n");
    }
    StellarObject* star = new StellarObject();
    assetmanager->GetObjectFromAsset("sphere",star);
    Object* highlight = assetmanager->GetObjectFromAsset("sunhighlight");
    highlight->material_slot[0] = 1;
    star->AttachChild(highlight);
    star->stellarbody = new StellarBody();
    star->stellarbody->type = BODY_STAR;
    star->stellarbody->likelyhood = 1.0;
    star->stellarbody->colony = GenerateNewStarColony();
    star->stellarbody->colony->name = "Star Colony " + std::to_string(star->GetID());
    return star;
}

StellarObject* StellarObject::CreateNewShip(AssetManager* assetmanager){
    if (!assetmanager){
        debug->Fatal("StellarObject::CreateNewShip with no assetmanager\n");
    }
    StellarObject* ship = new StellarObject();
    assetmanager->GetObjectFromAsset("ship",ship);
    ship->material_slot[0] = 2;
    ship->stellarbody = new StellarBody();
    ship->stellarbody->type = BODY_SHIP;
    ship->stellarbody->colony = GenerateNewShipColony();
    ship->stellarbody->colony->name = "Ship Colony " + std::to_string(ship->GetID());
    return ship;
}

void StellarObject::UpdatePosition(){
    if (stellarbody){
        if (stellarbody->f_updatevisual){
            vec2 c = stellarbody->coordinate;
            vec3 p = vec3(c.x,0,c.y);
            SetPosition(p);
            //Update rotation from heading
            float theta = stellarbody->heading.angle();
            quat q; q.set_rotation(vec3(0,-1,0),theta + TYPE_PI/2);
            SetRotation(q);
        }else{
            vec3 p = GetPosition();
            stellarbody->coordinate = vec2(p.x,p.z);
        }
        stellarbody->f_updatevisual = false;
    }
}

void Route::Setup(StellarBody* _start, StellarBody* _end){
    start = _start;
    end = _end;
}

float Route::GetDistance(){
    if ((!start) || (!end))
        return 0;
    return (end->coordinate - start->coordinate).length();
}

void Route::Reverse(){
    StellarBody* t = end;
    end = start;
    start = t;
}

//Creates a route from a to b. Adding sufficient path pieces to cover the path.
void RouteObject::SetupNewRoute(StellarObject* from, StellarObject* to, AssetManager* assetmanager){
    name = "Route " + from->name + " -> " + to->name;

    //We create a new route
    route = new Route();
    route->Setup(from->stellarbody,to->stellarbody);

    float dist = route->GetDistance();
    debug->Info("Route Distance = %.2f\n",dist);

    //Create the segments
    int num_segments = (dist / 2) + 1;
    for (int s=0;s<num_segments;s++){
        float k = float(s) / (float)num_segments;
        //Use a bezier to place objects along the path.
        //Alternatively, we use a single square to the bezier extents, and use a shader.
        Object* segment = assetmanager->GetObjectFromAsset("plane");
        if(!segment){
            debug->Fatal("Unable to load asset for route\n");
        }
        AttachChild(segment);
    }

    UpdateRoute();
}

//Does not rebuild segments, only updates their positions and rotation
void RouteObject::UpdateRoute(){
    //Assume all the children are segments.
    int num_segments = children.size();

    Bezier2D b = Bezier2D(2);
    b.AddNewPoint(vec2(0,0));
    b.AddNewPoint(route->end->coordinate - route->start->coordinate);

    //We start at from.
    vec2 startcoord = route->start->coordinate;
    SetPosition(vec3(startcoord.x,0,startcoord.y));

    for (int s=0;s<num_segments;s++){
        Object* segment = GetChild(s);
        if (!segment){
            debug->Fatal("Died while iterating over children\n");
        }
        float k = float(s) / (float)num_segments;

        segment->SetScale(vec3(0.4,1,0.15));
        vec2 p  = b.Lerp(k);
        segment->SetPosition(vec3(p.x,0,p.y));

        float theta = b.GetAngle(k);
        quat q; q.set_rotation(vec3(0,-1,0),theta);
        segment->SetRotation(q);
    }
}

void StellarObject::PlaceOnRoute(RouteObject* object_route){
    //Get stellarbody, route
    StellarBody* body = this->stellarbody;
    Route* route = object_route->route;

    if (route && body){
        body->PlaceOnRoute(route);
    }
}

void StellarBody::PlaceOnRoute(Route* _route){
    route = new Route(_route);
    UpdateRouteInfo();
}

//If this is a ship, with a route.. Follow the route
//This function really doesnt do anything.
void StellarBody::UpdateRouteInfo(){
    if (!route) return;
    if (!route->start) return;
    if (!route->end) return;

    //It'd be nice to find the closest point on the route
    //TODO: Reuse the bezier?
    Bezier2D b = Bezier2D(2);
    b.AddNewPoint(route->start->coordinate);
    b.AddNewPoint(route->end->coordinate);

    vec2 clostest_point = b.FindClosest(coordinate,20);
    //debug->Info("clostest_point = %.2f, %.2f\n",clostest_point.x,clostest_point.y);

    float dist = coordinate.distance(clostest_point);
    //debug->Info("Updating route. Off Route Dist = %.2f\n",dist);
    if (dist > 4.0f){
        //debug->Info(" -Snapped to route.\n");
        //Let's move ourselves to that nextuh spottuh.
        //coordinate = clostest_point;
        //We need to know that the underlying data (coordinate) was modified to the visulas don't override.
        //f_updatevisual = true;
    }
}

void StellarBody::MoveForward(float delta){
    vec2 d = delta * heading;
    coordinate += d;
    f_updatevisual = true;
}

void StellarBody::Turn(float delta){
    heading.rotate(delta);
    f_updatevisual = true;
}


//Just going to move to the endpoint at some speed
void StellarBody::FollowRoute(){
    if (!route) return;
    if (!route->end) return;

    vec2 end = route->end->coordinate;          // Target
    vec2 dir = (end-coordinate).normalize();    // Heading
    heading = dir;
    MoveForward(0.01f);

    float dist = route->end->coordinate.distance(coordinate);
    if (dist < 0.1f){
        //Maybe create a buy order or something.
        ResourceSlot order;
        order.resource.type = RESOURCE_FOOD;
        order.amount = 20;

        PickupResource(order,route->end);

        Contract c = route->end->colony->GetContract(RESOURCE_FOOD, 20, BUY_CONTRACT);
        //c.Fulfill();

       // Contract c;
        c.offer.resource.type = RESOURCE_FOOD;
        c.offer.amount = 20;

        route->Reverse();
    }

}

//Pickup resource from another stellar object.
void StellarBody::PickupResource(ResourceSlot& order, StellarBody* target){
    if (!colony) return;

    //First find the resource in the target colony.
    ResourceSlot* slot = FindResourceInSlots(target->colony->resource_slots,order.resource.type);
    if (!slot){
        debug->Err("Failed to pickup resource.\n");
        return;
    }

    int taken = slot->TakeResource(order.amount);
    debug->Info("Picked up %i resource\n",taken);
    order.amount = taken;

    AddResourceToSlots(colony->resource_slots,order);
}

StellarBody* StellarBody::FindClosest(std::vector<StellarBody*>&list, int target_type){
    float mindist = 9999;
    StellarBody* closest = NULL;
    for (StellarBody* body:list){
        if (body == this){
            continue;
        }
        if (body->type != target_type){
            continue;
        }
        float dist = coordinate.distance(body->coordinate);
        if (dist < mindist){
            closest = body;
            mindist = dist;
        }
    }
    return closest;
}