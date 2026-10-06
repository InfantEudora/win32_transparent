"""
Chasm's seed sweep: generate seeds one after another and run every check on each - the way a change
to the generator (ChasmLayout.cpp) or the pinning (Grid.cpp) is proved on more than one map.

Needs a DEBUG chasm running with its MCP server on the given port (the checks are debug only):

    ./build/chasm_nophysics.exe --minimized --mcp-port 8769 >/dev/null 2>stderr.log &
    python apps/chasm/tools/chasm_seed_sweep.py [first] [last] [port]

Prints a line per seed (layout summary, steps, sides) and every failing check but `pinned` - the
pinned hashes are for seeds 1-3 at the time they were pinned; the hashes of seeds 1-3 are printed so
they can be re-pinned when a change is meant to move them. Ends with the failures by check. Exit code
0 when nothing failed.
"""
import json
import sys
import urllib.request

FIRST = int(sys.argv[1]) if len(sys.argv) > 1 else 1
LAST = int(sys.argv[2]) if len(sys.argv) > 2 else 60
PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 8769


def call(name, args):
    req = urllib.request.Request("http://127.0.0.1:%d/mcp" % PORT,
                                 data=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                                  "params": {"name": name, "arguments": args}}).encode(),
                                 headers={"Content-Type": "application/json"})
    return json.loads(json.loads(urllib.request.urlopen(req, timeout=300).read())["result"]["content"][0]["text"])


def main():
    tally = {}
    for seed in range(FIRST, LAST + 1):
        g = call("chasm_generate", {"seed": seed, "defaults": True})
        r = call("chasm_check", {"max_issues": 3})
        bad = [x for x in r["results"] if x["result"] == "fail" and x["name"] != "pinned"]
        lay = g.get("layout", {})
        det = {x["name"]: x["detail"] for x in r["results"]}
        print("seed %2d: attempts %s, balconies E%s W%s, ledges %s, shards %s, columns %s, rivers %s | %s | %s" % (
            seed, lay.get("attempts"), lay.get("balconies_east"), lay.get("balconies_west"), lay.get("ledges"),
            lay.get("shards"), lay.get("columns"), lay.get("rivers"), det.get("steps", "")[:60],
            det.get("sides", "")[:50]))
        for x in bad:
            tally[x["name"]] = tally.get(x["name"], 0) + 1
            print("    FAIL", x["name"], "-", x["detail"][:400])
        if seed <= 3:
            print("    hash", r.get("hash"))
    print("failures:", sum(tally.values()), tally)
    return 1 if tally else 0


if __name__ == "__main__":
    sys.exit(main())
