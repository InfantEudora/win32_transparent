#include "LLMTokenizer.h"

#include <stdio.h>
#include <string.h>

#define TEKKEN_MAGIC 0x6e656b74 //"tken", the start of a Tekken tokenizer file (export_tekken.py in llama_c)

bool LLMTokenizer::Load(const std::string& path, int vocab_size_, std::string& error){
    *this = LLMTokenizer();
    FILE* file = fopen(path.c_str(),"rb");
    if (!file){
        error = "cannot open " + path;
        return false;
    }
    auto fail = [&](const std::string& why){
        fclose(file);
        vocab.clear();
        error = why;
        return false;
    };
    uint32_t first = 0;
    if (fread(&first,4,1,file) != 1){
        return fail("empty tokenizer file");
    }
    vocab_size = vocab_size_;
    f_tekken = first == TEKKEN_MAGIC;
    if (f_tekken){
        //a Tekken file has the vocab size in it, then the number of control tokens and the max length
        int header[3];
        if (fread(header,4,3,file) != 3){
            return fail("truncated tokenizer header");
        }
        if (header[0] != vocab_size){
            return fail("the tokenizer has " + std::to_string(header[0]) + " tokens, the model " + std::to_string(vocab_size));
        }
        n_special = header[1];
        max_token_length = header[2];
    }else{
        max_token_length = (int)first;
    }
    vocab.resize(vocab_size);
    scores.resize(vocab_size);
    for (int i = 0;i < vocab_size;i++){
        int len = 0;
        if (fread(&scores[i],4,1,file) != 1 || fread(&len,4,1,file) != 1){
            //A sentencepiece file does not say how many tokens it has, so a tokenizer that does
            //not belong to the model usually shows up here, as a file that ends too early.
            return fail(path + " ends after " + std::to_string(i) + " tokens, but the model has " +
                        std::to_string(vocab_size) + " - is it this model's tokenizer?");
        }
        vocab[i].resize(len);
        if (len > 0 && fread(&vocab[i][0],1,len,file) != (size_t)len){
            return fail("truncated tokenizer");
        }
    }
    if (f_tekken){
        int n_ranges = 0;
        if (fread(&n_ranges,4,1,file) != 1){
            return fail("truncated tokenizer (no Unicode ranges)");
        }
        range_start.resize(n_ranges);
        range_class.resize(n_ranges);
        for (int i = 0;i < n_ranges;i++){
            int r[2];
            if (fread(r,4,2,file) != 2){
                return fail("truncated tokenizer (Unicode ranges)");
            }
            range_start[i] = r[0];
            range_class[i] = (uint8_t)r[1];
        }
    }else{
        //sentencepiece: the control tokens come before the bytes <0x00>..<0xFF>, which start at 3
        //for Llama 2 and Mistral 7B v0.2. Mistral 7B v0.3 has [INST], [TOOL_CALLS] etc. in there
        n_special = 3;
        for (int i = 0;i < vocab_size;i++){
            if (vocab[i] == "<0x00>"){
                n_special = i;
                break;
            }
        }
    }
    fclose(file);
    for (int i = 0;i < n_special;i++){
        if (vocab[i] == "[TOOL_CALLS]"){
            tool_calls = i;
        }
    }
    //Every token the encoder can produce by merging: not the control tokens.
    lookup.reserve(vocab_size);
    for (int i = n_special;i < vocab_size;i++){
        lookup.emplace(vocab[i],i);
    }
    return true;
}

int LLMTokenizer::Lookup(const std::string& s) const{
    auto it = lookup.find(s);
    return it == lookup.end() ? -1 : it->second;
}

int LLMTokenizer::ControlToken(const char* s) const{
    if (*s != '[' && *s != '<'){
        return -1;
    }
    int best = -1;
    size_t best_len = 0;
    for (int id = 0;id < n_special;id++){
        size_t len = vocab[id].size();
        if (len > best_len && strncmp(s,vocab[id].c_str(),len) == 0){
            best = id;
            best_len = len;
        }
    }
    return best;
}

std::string LLMTokenizer::Decode(int prev_token, int token) const{
    const std::string& piece = vocab[token];
    //Tekken tokens are the raw bytes, a character can be split over several tokens
    if (f_tekken){
        return piece;
    }
    //following BOS (1), the sentencepiece decoder strips any leading whitespace
    if (prev_token == 1 && !piece.empty() && piece[0] == ' '){
        return piece.substr(1);
    }
    //raw byte tokens look like <0x01>
    unsigned int byte_val;
    if (piece.size() == 6 && sscanf(piece.c_str(),"<0x%02X>",&byte_val) == 1){
        return std::string(1,(char)byte_val);
    }
    return piece;
}

void LLMTokenizer::BpeMerge(std::vector<int>& tokens, size_t start) const{
    //merge the best consecutive pair each iteration, according to the scores
    while (true){
        float best_score = -1e10f;
        int best_id = -1;
        size_t best_idx = 0;
        for (size_t i = start;i + 1 < tokens.size();i++){
            int id = Lookup(vocab[tokens[i]] + vocab[tokens[i + 1]]);
            if (id != -1 && scores[id] > best_score){
                best_score = scores[id];
                best_id = id;
                best_idx = i;
            }
        }
        if (best_id == -1){
            break;
        }
        tokens[best_idx] = best_id;
        tokens.erase(tokens.begin() + best_idx + 1);
    }
}

//--- Tekken -------------------------------------------------------------------------------------
//The regex (from tekken.json), with U = [\p{Lu}\p{Lt}\p{Lm}\p{Lo}\p{M}] and L = [\p{Ll}\p{Lm}\p{Lo}\p{M}]:
//  [^\r\n\p{L}\p{N}]?U*L+ | [^\r\n\p{L}\p{N}]?U+L* | \p{N} | ?[^\s\p{L}\p{N}]+[\r\n/]* | \s*[\r\n]+ | \s+(?!\S) | \s+
//The classes of the code points, see export_tekken.py:
enum { TK_OTHER, TK_UPPER, TK_LOWER, TK_LETTER, TK_MARK, TK_NUMBER, TK_SPACE };

#define TK_IS_LETTER(c) ((c) == TK_UPPER || (c) == TK_LOWER || (c) == TK_LETTER)
#define TK_IS_U(c) ((c) == TK_UPPER || (c) == TK_LETTER || (c) == TK_MARK)
#define TK_IS_L(c) ((c) == TK_LOWER || (c) == TK_LETTER || (c) == TK_MARK)
#define TK_IS_NEWLINE(cp) ((cp) == '\r' || (cp) == '\n')

int LLMTokenizer::TekkenClass(int cp) const{
    //binary search for the last range that starts at or before cp
    int lo = 0, hi = (int)range_start.size() - 1;
    while (lo < hi){
        int mid = (lo + hi + 1) / 2;
        if (range_start[mid] <= cp){ lo = mid; }else{ hi = mid - 1; }
    }
    return range_class[lo];
}

//The regex match at code point i of n (which is the end of the text, for the lookahead): returns its end.
static int TekkenWord(const int* cp, const uint8_t* cl, int i, int n){
    //U*L+ and U+L*, each first with the optional prefix and then without
    int prefix = !TK_IS_LETTER(cl[i]) && cl[i] != TK_NUMBER && !TK_IS_NEWLINE(cp[i]);
    for (int alt = 0;alt < 2;alt++){
        for (int skip = prefix;skip >= 0;skip--){
            int j = i + skip, k = j;
            while (k < n && TK_IS_U(cl[k])){ k++; }     //U* is greedy...
            for (int b = k;b >= j;b--){
                if (alt == 1 && b == j){ break; }       //U+ needs one
                //...and gives back until L+ can start (for U+L*, L* always matches)
                if (alt == 0 && !(b < n && TK_IS_L(cl[b]))){ continue; }
                while (b < n && TK_IS_L(cl[b])){ b++; }
                return b;
            }
        }
    }
    //\p{N}
    if (cl[i] == TK_NUMBER){ return i + 1; }
    //' ?[^\s\p{L}\p{N}]+[\r\n/]*'
    int j = i + (cp[i] == ' ' && i + 1 < n);
    if (cl[j] == TK_OTHER || cl[j] == TK_MARK){
        while (j < n && (cl[j] == TK_OTHER || cl[j] == TK_MARK)){ j++; }
        while (j < n && (TK_IS_NEWLINE(cp[j]) || cp[j] == '/')){ j++; }
        return j;
    }
    //what is left is whitespace
    int k = i;
    while (k < n && cl[k] == TK_SPACE){ k++; }
    //\s*[\r\n]+: up to the last newline in the whitespace
    for (int b = k - 1;b >= i;b--){
        if (TK_IS_NEWLINE(cp[b])){ return b + 1; }
    }
    //\s+(?!\S): all of it at the end of the text, else all but the last one, which goes with the next word
    if (k == n || k - 1 == i){ return k; }  //(and \s+ for a single one)
    return k - 1;
}

void LLMTokenizer::EncodeTekken(const std::string& text, std::vector<int>& tokens) const{
    //the code points of the text, their class and where they start
    int len = (int)text.size();
    std::vector<int> cp, start;
    std::vector<uint8_t> cl;
    cp.reserve(len + 1); start.reserve(len + 1); cl.reserve(len + 1);
    for (int i = 0;i < len;){
        unsigned char c = text[i];
        int size = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        int code = size == 1 ? c : c & (0xFF >> (size + 1));
        for (int k = 1;k < size;k++){
            unsigned char cc = i + k < len ? text[i + k] : 0;
            if ((cc & 0xC0) != 0x80){ size = 1; code = c; break; }   //invalid UTF-8: take the byte on its own
            code = (code << 6) | (cc & 0x3F);
        }
        start.push_back(i);
        cp.push_back(code);
        cl.push_back(size == 1 && c >= 0x80 ? TK_OTHER : TekkenClass(code));
        i += size;
    }
    int n = (int)cp.size();
    start.push_back(len);
    cp.push_back(0);        //the lookahead's sentinel, never matched
    cl.push_back(TK_OTHER);

    int i = 0;
    while (i < n){
        int special = ControlToken(text.c_str() + start[i]);
        if (special >= 0){
            tokens.push_back(special);
            int end = start[i] + (int)vocab[special].size();
            while (i < n && start[i] < end){ i++; }
            continue;
        }
        //the text up to the next control token is split into words on its own
        int seg_end = i + 1;
        while (seg_end < n && ControlToken(text.c_str() + start[seg_end]) < 0){ seg_end++; }
        while (i < seg_end){
            int end = TekkenWord(cp.data(),cl.data(),i,seg_end);
            std::string word = text.substr(start[i],start[end] - start[i]);
            i = end;
            //a word that is a token as a whole
            int id = (int)word.size() <= max_token_length ? Lookup(word) : -1;
            if (id != -1){
                tokens.push_back(id);
                continue;
            }
            //else start from its bytes, which are the first ranks, and merge
            size_t word_start = tokens.size();
            for (unsigned char b:word){
                tokens.push_back(n_special + b);
            }
            BpeMerge(tokens,word_start);
        }
    }
}

//--- sentencepiece ------------------------------------------------------------------------------

void LLMTokenizer::EncodeSentencepiece(const std::string& text, std::vector<int>& tokens) const{
    //text without control tokens, appended to tokens. The merging stays after start.
    size_t start = tokens.size();
    //add_dummy_prefix is true by default: a space before a non-empty text
    if (!text.empty()){
        tokens.push_back(Lookup(" "));
    }
    //one token per UTF-8 code point, or its bytes when the code point is not a token
    for (size_t i = 0;i < text.size();){
        size_t len = 1;
        while (i + len < text.size() && ((unsigned char)text[i + len] & 0xC0) == 0x80 && len < 4){
            len++;
        }
        std::string point = text.substr(i,len);
        int id = Lookup(point);
        if (id != -1){
            tokens.push_back(id);
        }else{
            //byte fallback: the bytes start after the control tokens
            for (unsigned char b:point){
                tokens.push_back(b + n_special);
            }
        }
        i += len;
    }
    BpeMerge(tokens,start);
}

std::vector<int> LLMTokenizer::Encode(const std::string& text, bool bos, bool eos) const{
    std::vector<int> tokens;
    if (bos){ tokens.push_back(1); }
    if (f_tekken){
        EncodeTekken(text,tokens);
    }else{
        /*
            Control tokens such as [INST] (Mistral 7B v0.3) become their token. The text in between
            is encoded on its own, without the one space that follows a control token:
            sentencepiece's dummy prefix takes its place, which gives the same tokens as Mistral's
            own template, e.g. "[INST] Hello[/INST]" = [INST] _Hello [/INST]
        */
        const char* s = text.c_str();
        bool f_after_control = false;
        while (true){
            const char* c = s;
            int id = -1;
            while (*c != '\0' && (id = ControlToken(c)) < 0){ c++; }
            if (f_after_control && *s == ' ' && s < c){ s++; }
            EncodeSentencepiece(std::string(s,c - s),tokens);
            if (id < 0){
                break;
            }
            tokens.push_back(id);
            s = c + vocab[id].size();
            f_after_control = true;
        }
    }
    if (eos){ tokens.push_back(2); }
    return tokens;
}
