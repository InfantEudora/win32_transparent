#ifndef _STATE_HASH_H_
#define _STATE_HASH_H_

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <type_traits>

/*
    A hash of simulation state, in named parts - docs/replay_determinism_plan.md.

    A replay is deterministic when every tick ends in the same state as last time. Comparing that
    state directly would mean writing all of it down, so each tick's is hashed instead, and the
    hashes of two runs compared: one 64-bit number per part per tick, where the first mismatch
    says both WHEN two runs parted and in WHAT.

    BIT-EXACT, NEVER ROUNDED. A float goes in as its four bytes. Rounding would not make the
    hash tolerant - a value that sits on a rounding boundary still flips - and it would hide a
    difference in the last bit that grows into a missed kick a hundred ticks later. The same exe
    on the same machine given the same input from the same state gives the same bits; anything
    else is a bug to find, not noise to filter.

    Add FIELDS, not whole structs: a struct's padding bytes are whatever was in memory, and would
    make two identical states hash differently. The template refuses anything that is not plain
    data, but it cannot see padding - that part is on the caller.

    FNV-1a, 64-bit: nothing here needs to resist an attacker, and it is a dozen lines.
*/
class StateHash{
public:
    struct Part{
        std::string name;
        uint64_t hash = FNV_OFFSET;
    };

    //Everything added from here to the next Begin goes into `name`'s part. The same name again
    //carries on in that part, so a part can be filled from several places.
    void Begin(const char* name){
        for (size_t i = 0; i < parts.size(); i++){
            if (parts[i].name == name){
                current = (int)i;
                return;
            }
        }
        Part p;
        p.name = name;
        parts.push_back(p);
        current = (int)parts.size() - 1;
    }

    void Bytes(const void* data, size_t size){
        if (current < 0){
            Begin("state");
        }
        uint64_t h = parts[current].hash;
        const unsigned char* b = (const unsigned char*)data;
        for (size_t i = 0; i < size; i++){
            h ^= b[i];
            h *= FNV_PRIME;
        }
        parts[current].hash = h;
    }

    template<typename T> void Add(const T& value){
        static_assert(std::is_trivially_copyable<T>::value,"StateHash::Add takes plain data - add its fields");
        Bytes(&value,sizeof(T));
    }
    void Add(const std::string& s){
        uint32_t n = (uint32_t)s.size();
        Add(n);                 //the length first, so "ab"+"c" and "a"+"bc" differ
        Bytes(s.data(),s.size());
    }

    //Every part folded together, in the order they were begun.
    uint64_t Total() const{
        uint64_t h = FNV_OFFSET;
        for (const Part& p : parts){
            for (int k = 0; k < 8; k++){
                h ^= (p.hash >> (k * 8)) & 0xFF;
                h *= FNV_PRIME;
            }
        }
        return h;
    }
    bool Empty() const { return parts.empty(); }
    void Clear(){ parts.clear(); current = -1; }

    //Sixteen hex digits - what the tools compare. Not a JSON number, which is a double and keeps
    //only 53 of the 64 bits.
    static std::string Hex(uint64_t h){
        char text[17];
        for (int i = 15; i >= 0; i--){
            text[i] = "0123456789abcdef"[h & 0xF];
            h >>= 4;
        }
        text[16] = 0;
        return std::string(text);
    }

    std::vector<Part> parts;

private:
    static constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
    static constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    int current = -1;
};

#endif
