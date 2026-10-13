#ifndef _LLM_CHAT_H_
#define _LLM_CHAT_H_

#include "Application.h"    //for json
#include "LLMModel.h"
#include "LLMTokenizer.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

/*
    What runq does with a model, on top of LLMModel: sampling, plain generation, scoring a text
    (loss mode), and the two-character duet with its closing tool calls. Ported from runq.c so
    the two can be run on the same files and compared; the output formats are runq's.

    RENDER THREAD ONLY, like the model it drives.
*/

//runq's sampler: temperature, then top-p, with runq's xorshift generator.
class LLMSampler{
public:
    LLMSampler(int vocab_size, float temperature, float topp, unsigned long long seed);
    //Takes the logits by value: sampling rewrites them into probabilities.
    int Sample(const float* logits);

private:
    int vocab_size;
    float temperature, topp;
    unsigned long long rng_state;
    std::vector<float> probs;
    std::vector<std::pair<float,int>> candidates;
    unsigned int RandomU32();
    float RandomF32();
};

struct LLMGenerateArgs{
    std::string prompt;
    int steps = 256;            //total positions, prompt included, like runq's -n in generate mode
    float temperature = 1.0f;
    float topp = 0.9f;
    unsigned long long seed = 0;
};

struct LLMDuetArgs{
    std::string scenario;       //the two characters: name line + system prompt, separated by a line "---"
    std::string tools;          //JSON list, one per character separated by "---", or one for both; empty for none
    std::string opening;        //the first character's first line, or empty
    int lines = 10;
    int max_tokens = 120;       //per line; a tool call may take 4 times this
    float temperature = 0.15f;
    float topp = 0.9f;
    unsigned long long seed = 0;
};

/*
    A chat with the model, one turn at a time, generated a few tokens per call of Step so a UI
    can show the reply as it grows. RENDER THREAD ONLY.

    The conversation lives in one of the model's contexts (the last one) as a list of the tokens
    fed so far. Other work may use that context in between - a duet, a loss run - so whenever
    that may have happened the owner calls Invalidate, and the next Send or Step feeds the whole
    conversation again first. Through the batch path that costs a fraction of a second.

    The template is the model's own: Ministral 3's [SYSTEM_PROMPT]...[INST]...[/INST], or
    Mistral 7B's [INST]...[/INST] with the system prompt in the first turn. An assistant turn is
    closed by EOS, fed at the start of the next user turn - as runq's chat mode does.
*/
class LLMChatSession{
public:
    struct Message{
        std::string role;   //"user" or "assistant"
        std::string text;
    };
    std::vector<Message> messages;
    std::string system_prompt;
    float temperature = 0.15f;
    float topp = 0.9f;
    int max_tokens = 512;
    unsigned long long seed = 1;

    bool f_generating = false;
    double tokens_per_second = 0.0;     //of the last reply
    int prompt_tokens = 0;              //fed for the last turn, a re-feed included
    double prompt_seconds = 0.0;

    void Reset();
    //Starts the assistant's reply to text; false with error when it cannot (no room left, ...).
    bool Send(LLMModel& model, LLMTokenizer& tokenizer, const std::string& text, std::string& error);
    //Generates for up to budget_ms, or until the reply ends.
    void Step(LLMModel& model, LLMTokenizer& tokenizer, double budget_ms);
    void Stop(){ f_generating = false; }
    void Invalidate(){ f_cache_valid = false; }
    int Used() const { return (int)tokens.size(); }

private:
    std::vector<int> tokens;        //everything fed into the context so far
    bool f_cache_valid = false;
    const float* logits = NULL;     //after the last token fed, while the cache is valid
    std::unique_ptr<LLMSampler> sampler;
    int generated = 0;
    int prev_token = 0;
    std::chrono::steady_clock::time_point reply_start;

    //Feeds new_tokens after what is already in the context, re-feeding the whole conversation
    //first if another job used the context since.
    bool Feed(LLMModel& model, const std::vector<int>& new_tokens);
};

json LLMGenerate(LLMModel& model, LLMTokenizer& tokenizer, const LLMGenerateArgs& args);
//Scores text the way runq's -m loss does: documents separated by lines "---", in a chat
//transcript only the replies scored.
json LLMLoss(LLMModel& model, LLMTokenizer& tokenizer, const std::string& text);
json LLMDuet(LLMModel& model, LLMTokenizer& tokenizer, const LLMDuetArgs& args);

#endif
