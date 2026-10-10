---
title: Store
aliases: storehouse
kind: buildings
status: built
summary: Holds goods for the whole colony. Beside a workplace it takes only that workplace's goods; on its own, whatever the player allows.
plans: economy_plan.md > As built: the simulation; economy_plan.md > Decided
code: Economy.cpp: Derive FindStore; Zones.h: ZoneBuilding
verified: 2026-10-09
---

## What it takes

- **Beside a workplace**, a store takes only that workplace's goods: [[Wood|wood]] beside a
  [[Woodcutter|woodcutter's hut]], [[Water|water]] beside a [[Water collector]], and every
  [[Food|food]] beside a [[Field]] (one plot away from the field is as close as a store can get). This
  holds for as long as they touch, and the player can't change it.
- **On its own**, a store takes whatever the player allows on its card, which is every good to start with,
  [[Provisions|provisions]] included.

Workers carry to the *nearest* store that takes their load and has room for all of it, so a store beside
a workplace is in effect that workplace's own.

## Room

A store holds its floor area (plot area x storeys) x {ECONOMY_STORE_ROOM} units, shared by every good in
it. It does nothing until it has been built ([[Construction]]).

## Who comes to it

- Workers bringing goods: [[Woodcutter|woodcutters]], [[Water collector|water carriers]], and
  [[Farmer|farmers]] once their own house has its share of the harvest.
- People carrying wood to [[Construction|building sites]].
- Families [[Fetching home|fetching]] food, water and firewood for their house. Nobody delivers to a house.
- Anyone with a pressing [[Thirst|thirst]] or hunger, to eat and drink there.

## Split or pulled down

If an erase splits a store, its stock is shared out by floor area. If a store is pulled down, what it
held is lost.

A [[Crate|crate]] lying about is a store of its own that villagers only take from, and builders carry its
goods here.

## Planned

A load that no store will take is to be put down as a [[Crate|crate]], instead of being carried around
until a store will take it - on the crate's page.

- [planned] Goods in four categories - food, water, materials, inputs - and a store's card grouped by them:
  a stock strip per category, and its "Takes" chips a line per category. Only food is ever summed.
