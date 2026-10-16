#ifndef _CHASM_STATS_H_
#define _CHASM_STATS_H_

#include <stdint.h>
#include <array>
#include <string>
#include <vector>
#include "Economy.h"
#include "Zones.h"

/*
    THE STATISTICS (docs/statistics_plan.md): what the Statistics page and chasm_stats show, worked out from a
    published state - the history the rules keep (Economy.h, THE HISTORY) and the colony as it stands. Any thread,
    view only: nothing here reaches the rules. Totals are summed from the journal and the ledger each time they are
    asked for, never kept, so they cannot disagree with what they sum; ChasmStatsCheck says whether the history
    agrees with the state it was kept beside.
*/

#define CHASM_STATS_CAUSES  4       //NEEDS_DIED_HUNGER .. NEEDS_DIED_ZOMBIE

//Days of food (GOODS_FOOD: provisions are the posted people's) and of water that a row's goods hold for its people
//at their shares - the figure the charts draw, and the page's now from a row made of the state as it stands.
void ChasmStatsDays(const EconomyDay& row, float& food, float& water);

//Since the history began: from the journal, and from the ledger's rows and today's.
struct ChasmStatsTotals{
    int born = 0;
    int died = 0;
    std::array<int,CHASM_STATS_CAUSES> died_of = {};
    int harvests = 0;
    std::array<int,GOOD_COUNT> harvested = {};
    int finished = 0;
    std::array<int,ZONE_KIND_COUNT> finished_kind = {};
    int settled = 0;                //settlements founded, a new colony's landing among them
    int seams = 0;                  //floatstone seams first seen
    int felled = 0;
    int broken = 0;
    int storeys = 0;
};
ChasmStatsTotals ChasmStatsSum(const EconomyHistory& h);

//A SEASON's bars: counted by the season its events and rows fall in, from the history's first to today's.
struct ChasmStatsSeason{
    int season = 0;                 //seasons since the calendar began: day / CALENDAR_DAYS_PER_SEASON
    int born = 0;
    int died = 0;
    int harvested = 0;              //units of every crop
    int felled = 0;
};
std::vector<ChasmStatsSeason> ChasmStatsSeasons(const EconomyHistory& h);

//NOW: the colony as it stands.
struct ChasmStatsSettlement{
    uint32_t id = 0;
    int people = 0;
    int beds = 0;
};
struct ChasmStatsNow{
    EconomyDay row;                 //what a ledger row would hold now: the people by stage, every good held
    float food_days = 0.0f;
    float water_days = 0.0f;
    int people = 0;
    int expecting = 0;
    int in_camp = 0;                //sleeping in a camp, or with no home (BUILD HOUSES's count)
    int houses = 0;                 //standing, and lived in
    int tents = 0;                  //a camp's plots
    int beds = 0;
    std::array<int,ZONE_KIND_COUNT> standing = {};      //by kind: whole
    std::array<int,ZONE_KIND_COUNT> building = {};      //...and with something still to build
    std::array<int,CHASM_STATS_CAUSES> dead_of = {};    //every death the colony remembers, by cause
    int dead = 0;
    std::vector<ChasmStatsSettlement> settlements;
};
ChasmStatsNow ChasmStatsNowOf(const EconomyState& e, const ZoneState& z);

/*
    THE CHECK (statistics_plan.md, "Check it three ways"): the history against the state it was kept beside -
      - the journal's deaths are the dead since it began, and its births everyone born in the colony since;
      - the trees felled and rocks broken it counted are the forest's, less what was down when it began;
      - each row's people are the row before's, and that day's births less its deaths - across a gap the date
        was set over, nothing is compared.
    Empty when all agree; else a line for each that does not.
*/
std::vector<std::string> ChasmStatsCheck(const EconomyState& e);

//A journal line in words - "Kaat De Wit was born", "a field gave 24 wheat" - its names from the state.
std::string ChasmJournalText(const EconomyState& e, const EconomyEvent& ev);
//A date for the page, short: "Y2 SUMMER 4"; and a season's: "Y2 SUMMER".
std::string ChasmStatsDate(uint64_t tick);
std::string ChasmStatsSeasonName(int season);
//A building kind as the page names it, with its plural: "woodcutter's hut", "woodcutters' huts".
const char* ChasmKindLabel(int kind, bool f_plural);

#endif
