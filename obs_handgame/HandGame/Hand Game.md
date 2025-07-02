
Created in Obsidian to track ideas for simple but also complicated game.

# Story

A witch up the river has been concocting soups and dumping waste in the river.  You've fallen in the river, and your body has magically dissolved until only your hand remained.

Luckily, you are still able to control it. Your severed hand.

You have just crawled your way out of a river up onto a cliff, broke through a fence and are now at the entrance of a vast castle dungeon courtyard.
# Goal

Obviously, you need to find a way to become whole again. But a hand can't see, nor can it speak.
It needs to be cute-ish, but also have a dark and doomy vibe.

# Characters

There's this bony avatar that will guide you maybe? She's wearing headphones and like's to play tunes on her keyboard for dramatic vibes. She likes to play on words, so most hints or tasks that she gives you have a dual meaning.

For doors, you need keys. And she needs keys for her keyboard.

# Mechanics

You can move the hand around, and creep forward. Climb op stairs and knock on doors. Maybe like in Munchkin, every room can be occupied by a character or be empty with some goodies inside?

### Workflow

We have AI. That can generate anything from thin air and humongous amounts of power. Usefull for inspiration. Less usefull for anything else at this point.

We can low poly create a huge bunch of assets really quickly.

Actually, speech is pretty good as well.
https://elevenlabs.io/text-to-speech

Using that with Laura.

Also has some sounds and can generate them.

### Level

Before any of this shit can happen, we need a way to generate, edit and store castle/dungeon layouts.
As a basis, it will use a terrain with simple square grids. But it'll be possible to tunnel through things. Basically a minecraft block only the block heights differ.

### Blender Files

- Using 'Tiles.blend' for a proof of concept super level where all assets get imported and are in semi-unfinished state but it should reflect the look and feel of what is possible.
- You can take any object out of 'Tiles.blend' and put it in 'tiles_hand.blend' the idea it that any asset gets put in it's own group, all use the same few materials so the single file can be exported into a single glb file.
- ai_stone_gate_avatar_keyboard.blend for trying to animate an avatar that will eventualy guide you through the game.

### Todo's

- [ ] Generate a room with walls and a door.
- [ ] Add a single light source in that room.
- [ ] Be able play an animation to open and close the door by clicking it.
- [ ] Play a sound opening and closing.
- [ ] Generate a hallway with an adjacent room. Have this room have a single light source, not infuenced by the other light source.
- [ ] Have a third room that can be entered through stairs, so it needs to be on a different height/level.

### Tiling System

Tiles are 1x1x0.5 by default. Most logical would be that an edge of a single tile is also an edge for the adjacent tile. And the corners will have to be pillars of some sort. Keep it simple.
