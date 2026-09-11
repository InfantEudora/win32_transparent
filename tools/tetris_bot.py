#!/usr/bin/env python
"""
Plays APP=Tetris through its own MCP tools over HTTP, with no human and no window focus.

This exists to prove two things the brief asks for: that the game is playable entirely through
the tool surface, and that a scripted "player" is indistinguishable from a person - it presses
the same keys, through InputController::HoldKey, and the simulation never learns the difference.

    python tools/tetris_bot.py --pieces 40 [--url http://127.0.0.1:8765/mcp] [--seed 7]

Note the 127.0.0.1 rather than localhost: the MCP HTTP server binds IPv4 only, and on a machine
where localhost resolves to ::1 first every single call pays a ~2 second connect fallback. That
is 130x on a driver like this one - see docs/tetris_findings.md.

It is a plain greedy bot: for each piece it tries every rotation in every column, drops it in a
copy of the board, and scores the result on aggregate height, holes, bumpiness and lines cleared
(the classic Lee/Dellacherie weights). Good enough to clear a lot of lines, which is the point -
it is a test driver, not an AI.
"""

import argparse
import json
import sys
import urllib.request

# Piece cells as (x, y) offsets from the bottom-left of the bounding box, y up. The same tables
# tetris/Tetromino.cpp builds from its ASCII art - kept here so the bot can predict a landing
# without asking the game.
SHAPES = {
    "I": [[(0,2),(1,2),(2,2),(3,2)], [(2,3),(2,2),(2,1),(2,0)],
          [(0,1),(1,1),(2,1),(3,1)], [(1,3),(1,2),(1,1),(1,0)]],
    "J": [[(0,2),(0,1),(1,1),(2,1)], [(1,2),(2,2),(1,1),(1,0)],
          [(0,1),(1,1),(2,1),(2,0)], [(1,2),(1,1),(0,0),(1,0)]],
    "L": [[(2,2),(0,1),(1,1),(2,1)], [(1,2),(1,1),(1,0),(2,0)],
          [(0,1),(1,1),(2,1),(0,0)], [(0,2),(1,2),(1,1),(1,0)]],
    "O": [[(1,2),(2,2),(1,1),(2,1)]] * 4,
    "S": [[(1,2),(2,2),(0,1),(1,1)], [(1,2),(1,1),(2,1),(2,0)],
          [(1,1),(2,1),(0,0),(1,0)], [(0,2),(0,1),(1,1),(1,0)]],
    "T": [[(1,2),(0,1),(1,1),(2,1)], [(1,2),(1,1),(2,1),(1,0)],
          [(0,1),(1,1),(2,1),(1,0)], [(1,2),(0,1),(1,1),(1,0)]],
    "Z": [[(0,2),(1,2),(1,1),(2,1)], [(2,2),(1,1),(2,1),(1,0)],
          [(0,1),(1,1),(1,0),(2,0)], [(1,2),(0,1),(1,1),(0,0)]],
}
BOX = {"I": 4, "J": 3, "L": 3, "O": 3, "S": 3, "T": 3, "Z": 3}

WIDTH = 10
HEIGHT = 20

_request_id = [0]


def call(url, name, args=None):
    _request_id[0] += 1
    payload = {
        "jsonrpc": "2.0",
        "id": _request_id[0],
        "method": "tools/call",
        "params": {"name": name, "arguments": args or {}},
    }
    request = urllib.request.Request(
        url, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        body = json.load(response)
    if "error" in body:
        raise RuntimeError(body["error"])
    return json.loads(body["result"]["content"][0]["text"])


def occupancy(state):
    """The board as a set of occupied (x, y), y counting UP - the game's own convention. The
    ASCII comes top row first, and lowercase is the active piece, which is not settled yet."""
    cells = set()
    for row_index, row in enumerate(state["board"]):
        y = HEIGHT - 1 - row_index
        for x, character in enumerate(row):
            if character != "." and not character.islower():
                cells.add((x, y))
    return cells


def drop(cells, shape, x):
    """Lowest legal bounding-box bottom row for `shape` placed at column x, or None."""
    for offset_x, offset_y in shape:
        if not 0 <= x + offset_x < WIDTH:
            return None
    y = HEIGHT
    while y > -4:
        if any((x + dx, y - 1 + dy) in cells or y - 1 + dy < 0 for dx, dy in shape):
            break
        y -= 1
    for dx, dy in shape:
        if y + dy >= HEIGHT:
            return None
    return y


def evaluate(cells, shape, x, y):
    """Dellacherie's evaluation. The plain height/holes/bumpiness version ties on an empty board,
    and a tie broken by column order stacks every piece in the left corner - which is exactly what
    the first version of this bot did."""
    heights = [0] * WIDTH
    for column in range(WIDTH):
        for row in range(HEIGHT - 1, -1, -1):
            if (column, row) in cells:
                heights[column] = row + 1
                break

    landing_height = y + sum(dy for _, dy in shape) / 4.0
    complete = [row for row in range(HEIGHT) if all((column, row) in cells for column in range(WIDTH))]
    eroded = len(complete) * sum(1 for dx, dy in shape if y + dy in complete)

    row_transitions = 0
    for row in range(HEIGHT):
        previous = True  # the wall counts as filled
        for column in range(WIDTH):
            filled = (column, row) in cells
            if filled != previous:
                row_transitions += 1
            previous = filled
        if not previous:
            row_transitions += 1

    column_transitions = 0
    for column in range(WIDTH):
        previous = True  # the floor counts as filled
        for row in range(HEIGHT):
            filled = (column, row) in cells
            if filled != previous:
                column_transitions += 1
            previous = filled

    holes = sum(
        1
        for column in range(WIDTH)
        for row in range(heights[column])
        if (column, row) not in cells
    )

    wells = 0
    for column in range(WIDTH):
        for row in range(HEIGHT - 1, -1, -1):
            if (column, row) in cells:
                break
            left = column == 0 or (column - 1, row) in cells
            right = column == WIDTH - 1 or (column + 1, row) in cells
            if left and right:
                depth = 1
                while row - depth >= 0 and (column, row - depth) not in cells:
                    depth += 1
                wells += depth * (depth + 1) // 2
                break

    return (-4.5 * landing_height + 3.4 * eroded - 3.2 * row_transitions
            - 9.3 * column_transitions - 7.9 * holes - 3.4 * wells)


def choose(state):
    piece = state["piece"]
    if piece not in SHAPES:
        return None
    cells = occupancy(state)
    best = None
    for rotation in range(4):
        shape = SHAPES[piece][rotation]
        for x in range(-1, WIDTH):
            y = drop(cells, shape, x)
            if y is None:
                continue
            placed = cells | {(x + dx, y + dy) for dx, dy in shape}
            score = evaluate(placed, shape, x, y)
            if best is None or score > best[0]:
                best = (score, rotation, x)
    return best


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8765/mcp")
    parser.add_argument("--pieces", type=int, default=30)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--restart", action="store_true")
    options = parser.parse_args()

    if options.restart or options.seed:
        state = call(options.url, "tetris_restart", {"seed": options.seed})
    else:
        state = call(options.url, "tetris_state")

    placed = 0
    while placed < options.pieces:
        state = call(options.url, "tetris_state")
        if state["phase"] == "gameover":
            print("game over after %d pieces, score %d, lines %d" % (placed, state["score"], state["lines"]))
            return 0
        if state["phase"] != "falling":
            continue

        target = choose(state)
        if target is None:
            continue
        _, rotation, target_x = target

        for _ in range(rotation):
            state = call(options.url, "tetris_input", {"action": "rotate_cw", "ticks": 1})
        # A kick can move the piece sideways, so where it ended up is read back rather than assumed.
        guard = 0
        while state["phase"] == "falling" and state["piece_x"] != target_x and guard < 12:
            action = "left" if state["piece_x"] > target_x else "right"
            state = call(options.url, "tetris_input", {"action": action, "ticks": 1})
            guard += 1

        state = call(options.url, "tetris_input", {"action": "hard_drop", "ticks": 1})
        placed += 1
        print(
            "piece %-3d %s -> rot %d col %-3d  score %-7d lines %-3d level %d  debris %d"
            % (placed, target and state.get("piece") or "?", rotation, target_x,
               state["score"], state["lines"], state["level"], state["debris_bodies"])
        )

    final = call(options.url, "tetris_state")
    print("done: score %d, lines %d, level %d, pieces %d"
          % (final["score"], final["lines"], final["level"], final["pieces_placed"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
