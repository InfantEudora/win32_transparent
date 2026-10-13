#ifndef _LLM_TOKENIZER_H_
#define _LLM_TOKENIZER_H_

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

/*
    The tokenizers of runq (llama_c), ported to C++ unchanged in behaviour: the same .bin files,
    the same token ids for the same text. Two kinds:

      - TEKKEN (Ministral 3, Mistral NeMo): a tiktoken-style byte-level BPE. The file starts with
        the magic "tken". The text is split into words by the regex in tekken.json - written out
        by hand here, as in runq - and each word that is not a token as a whole is built up from
        its bytes, merging the pair with the lowest rank first.
      - SENTENCEPIECE (Llama 2, Mistral 7B): Karpathy's tokenizer.bin format, a BPE over code
        points with byte fallback and a dummy-prefix space.

    In both, control tokens such as [INST] and [TOOL_CALLS] written out in the text become their
    token, and the text between them is encoded on its own. Every id below n_special is a
    control token; the 256 single bytes follow.

    Decode returns a token's bytes as they are; a UTF-8 character may be split over tokens.
*/
class LLMTokenizer{
public:
    bool Load(const std::string& path, int vocab_size, std::string& error);
    bool IsLoaded() const { return !vocab.empty(); }

    //BOS (1) first and/or EOS (2) last, if asked.
    std::vector<int> Encode(const std::string& text, bool bos, bool eos) const;
    //The text of token, following prev_token (sentencepiece drops the space after BOS).
    std::string Decode(int prev_token, int token) const;

    //The control token written out at s, such as [INST], or -1.
    int ControlToken(const char* s) const;

    bool f_tekken = false;
    int vocab_size = 0;
    int n_special = 0;          //control tokens come first, then the bytes
    int tool_calls = -1;        //the id of [TOOL_CALLS], or -1 for a model without tool calls
    int max_token_length = 0;
    std::vector<std::string> vocab;
    std::vector<float> scores;  //sentencepiece: merge score; tekken: -rank

private:
    std::unordered_map<std::string,int> lookup;     //every token from n_special on
    std::vector<int> range_start;                   //tekken: Unicode classes for its regex
    std::vector<uint8_t> range_class;

    int Lookup(const std::string& s) const;
    void BpeMerge(std::vector<int>& tokens, size_t start) const;
    int TekkenClass(int cp) const;
    void EncodeTekken(const std::string& text, std::vector<int>& tokens) const;
    void EncodeSentencepiece(const std::string& text, std::vector<int>& tokens) const;
};

#endif
