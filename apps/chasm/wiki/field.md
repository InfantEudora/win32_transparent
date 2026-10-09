---
title: Field
aliases: fields, farmer, farmers, land, harvest, sowing, fallow
kind: buildings
status: built
summary: Painted on coarse cells and farmed by the family whose land it is. Sown with a crop, grown through the warm seasons, harvested home first.
plans: fields_plan.md > Decided; fields_plan.md > The frost; land_plan.md > As built; land_plan.md > Fair shares
code: Economy.cpp: SettleLand LandPlaces GoSow FetchHarvest PickHarvest EconomyFieldSowNow; Economy.h: EconomyField ECONOMY_CROP_GRACE
verified: 2026-10-09
---

A field is painted over **coarse cells**, each on flat ground, and buildings keep a plot away from it.
Placed in play it is a [[Construction|building site]] first, at {ECONOMY_FIELD_WOOD_PER_CELL} [[Wood|wood]]
a cell. Then it stands **empty** until it is sown.

## Whose land

Every field belongs to a [[House|house]] as its **land**, or to the [[Camp]] while no house is lived in.
It goes to the house the player picks on its card if that house is lived in, otherwise to the nearest
lived-in house whose farmer can walk to it. The land is the family's workplace: one **farmer** for every
{ECONOMY_FARMER_CELLS} cells or so of whole fields, always from the family itself. A field whose share has
no farmer lies **fallow**: nothing is sown and nothing grows.

## A field's year

1. **Sow.** Its farmer walks out and sows it, {ECONOMY_SOW_SECONDS:#seconds} a cell at an average pace
   ([[Skills|husbandry]]). Sowing comes before anything else the farmer does, and uses no seed.
2. **The crop** is the player's starting crop, then the field's *next crop*, which is AUTO (the most food)
   unless the player picks one ([[Food]]). A field left without a starting crop waits
   {ECONOMY_CROP_GRACE:days} for the player, then starts with AUTO.
3. **Grow** for the crop's days, faster with a better farmer, but only while it has one, and never in
   winter or under snow ([[Seasons]]).
4. **The frost.** Whatever still grows on winter's first day is lost. So a farmer only sows a crop that
   will ripen in time. AUTO picks the most food among those, and a chosen crop that can't make it waits
   for spring.
5. **Harvest.** The crop goes into the field's own stock and the field is empty again.

## The harvest

The land's farmers carry it in, {ECONOMY_LOAD} at a time, **home first**: into their own house while it
is short of food and not ahead of the colony (fair shares, [[Fetching home]]). The camp's land goes
into the camp's supplies. The rest goes to a [[Store]] that takes food (a store beside a field takes every
food), where other houses fetch it. Anyone with pressing hunger may eat straight from a harvest ([[Health]]).
