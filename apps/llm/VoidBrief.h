#ifndef _VOID_BRIEF_H_
#define _VOID_BRIEF_H_

#include "Application.h"    //for json

#include <chrono>
#include <string>
#include <vector>

/*
    The Void's brief (docs/void_plan.md): a running Chasm, read through its MCP tools, written out as
    what a small model can weigh - short sections in words and days, the game's NEEDS ATTENTION rows
    as they are, and a lettered menu of what could be advised now, each option with what it leads to.

    The model only weighs and speaks. Everything that is a sum - days of water, when a crop ripens,
    whether the stores hold what a site waits for - is worked out here, because step 0 showed a 3B
    model reading every number in an option as a shortage. For the same reason an option that saves
    nothing says so.

    Chasm gives its own figures for that since 2026-10-14 - read-only MCP output added for the Void:
    `held`, `crops`, `camp_growing_days_left`, `firewood`, `people.in_camp`, op:buildings - and they are
    what is used. The copies of its rules here (DaysHeld's sum, the crops' days, the shares people eat
    at) are only for a Chasm from before then, and drift if Chasm's rules change.

    No GL and no network: plain JSON in, text out, so it can be checked against the step-0 briefs.
*/

//The four replies a read is made of, as Chasm's tools return them.
struct VoidState{
    json time;          //chasm_time
    json economy;       //chasm_economy op status
    json issues;        //chasm_issues
    json zombies;       //chasm_zombie op status; empty from a Chasm without it
    json buildings;     //chasm_economy op buildings; empty from a Chasm without it
};

struct VoidBrief{
    bool f_read = false;            //false until a read succeeded; error says why not
    std::string error;
    std::chrono::steady_clock::time_point at;

    std::string date;               //"Year 1, spring, day 4"
    std::string text;               //the whole brief, the menu at its end - what the model is sent
    std::vector<std::string> options;   //the menu, A first
    /*
        Why the Void should speak now. Empty in a calm colony, where step 0 showed it inventing a
        problem rather than choosing nothing - so the asking side stays quiet then.
    */
    std::vector<std::string> reasons;

    //What the next brief compares with, for its SINCE LAST READ.
    uint64_t tick = 0;              //Chasm's calendar tick
    int deaths = 0;
    int notices_made = 0;
    std::vector<int> people;        //worker ids
};

//last: the previous brief, for what changed since; NULL (or one never read) for none.
VoidBrief VoidBuildBrief(const VoidState& state, const VoidBrief* last);

#endif
