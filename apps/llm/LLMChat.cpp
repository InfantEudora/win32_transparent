#include "LLMChat.h"

#include <algorithm>
#include <chrono>
#include <ctype.h>
#include <math.h>
#include <string.h>

typedef std::chrono::steady_clock Clock;
static double SecondsSince(Clock::time_point start){
    return std::chrono::duration<double>(Clock::now() - start).count();
}

//--- sampler (runq's, unchanged in behaviour) ---------------------------------------------------

LLMSampler::LLMSampler(int vocab_size_, float temperature_, float topp_, unsigned long long seed)
    : vocab_size(vocab_size_), temperature(temperature_), topp(topp_), rng_state(seed){
    probs.resize(vocab_size);
    candidates.reserve(vocab_size);
}

unsigned int LLMSampler::RandomU32(){
    //xorshift rng: https://en.wikipedia.org/wiki/Xorshift#xorshift.2A
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (unsigned int)((rng_state * 0x2545F4914F6CDD1Dull) >> 32);
}

float LLMSampler::RandomF32(){
    return (RandomU32() >> 8) / 16777216.0f;
}

int LLMSampler::Sample(const float* logits){
    if (temperature == 0.0f){
        return (int)(std::max_element(logits,logits + vocab_size) - logits);
    }
    //softmax of logits / temperature
    float max_val = -1e30f;
    for (int i = 0;i < vocab_size;i++){
        probs[i] = logits[i] / temperature;
        max_val = std::max(max_val,probs[i]);
    }
    float sum = 0.0f;
    for (int i = 0;i < vocab_size;i++){
        probs[i] = expf(probs[i] - max_val);
        sum += probs[i];
    }
    for (int i = 0;i < vocab_size;i++){
        probs[i] /= sum;
    }
    float coin = RandomF32();
    if (topp <= 0.0f || topp >= 1.0f){
        float cdf = 0.0f;
        for (int i = 0;i < vocab_size;i++){
            cdf += probs[i];
            if (coin < cdf){ return i; }
        }
        return vocab_size - 1;
    }
    //top-p: the smallest set of tokens whose probabilities exceed topp. Tokens below the cutoff
    //cannot be part of it, which saves sorting all of them.
    float cutoff = (1.0f - topp) / (vocab_size - 1);
    candidates.clear();
    for (int i = 0;i < vocab_size;i++){
        if (probs[i] >= cutoff){
            candidates.push_back({probs[i],i});
        }
    }
    std::sort(candidates.begin(),candidates.end(),[](const std::pair<float,int>& a, const std::pair<float,int>& b){
        return a.first > b.first;
    });
    float cumulative = 0.0f;
    int last = (int)candidates.size() - 1;
    for (int i = 0;i < (int)candidates.size();i++){
        cumulative += candidates[i].first;
        if (cumulative > topp){
            last = i;
            break;
        }
    }
    float r = coin * cumulative;
    float cdf = 0.0f;
    for (int i = 0;i <= last;i++){
        cdf += candidates[i].first;
        if (r < cdf){ return candidates[i].second; }
    }
    return candidates[last].second;
}

//--- helpers ------------------------------------------------------------------------------------

static std::string Trim(const std::string& s){
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])){ a++; }
    while (b > a && isspace((unsigned char)s[b - 1])){ b--; }
    return s.substr(a,b - a);
}

//Cuts text at its first line "---": the part before, and the part after (empty if there is none).
static bool SplitAtDashes(const std::string& text, std::string& first, std::string& second){
    for (size_t i = 0;i + 4 <= text.size();i++){
        if (text.compare(i,4,"\n---") == 0 && (i + 4 == text.size() || text[i + 4] == '\n')){
            first = text.substr(0,i);
            second = i + 5 <= text.size() ? text.substr(i + 5) : std::string();
            return true;
        }
    }
    first = text;
    second.clear();
    return false;
}

//The text without control characters other than whitespace - what runq prints of a line.
static std::string PrintableText(const std::string& s){
    std::string out;
    for (unsigned char c:s){
        if (!iscntrl(c) || isspace(c)){
            out += (char)c;
        }
    }
    return out;
}

//--- generate -----------------------------------------------------------------------------------

json LLMGenerate(LLMModel& model, LLMTokenizer& tokenizer, const LLMGenerateArgs& args){
    std::vector<int> tokens = tokenizer.Encode(args.prompt,true,false);
    int steps = std::min(args.steps,model.context_len);
    if ((int)tokens.size() >= steps){
        return json{ {"error","the prompt is " + std::to_string(tokens.size()) + " tokens, steps is " + std::to_string(steps)} };
    }
    LLMSampler sampler(model.config.vocab_size,args.temperature,args.topp,args.seed);
    auto start = Clock::now();
    //every prompt token but the last, in batches; logits are only needed from the last one on
    if (tokens.size() > 1){
        model.ForwardTokens(tokens.data(),(int)tokens.size() - 1,0);
    }
    double prompt_seconds = SecondsSince(start);
    auto gen_start = Clock::now();
    std::string generated;
    int pos = (int)tokens.size() - 1;
    int token = tokens[pos];
    int generated_count = 0;
    std::string stop = "length";
    while (pos < steps){
        const float* logits = model.Forward(token,pos);
        int next = sampler.Sample(logits);
        model.RecordBlocks(next);   //the block analysis, when it records; nothing otherwise
        pos++;
        //BOS delimits sequences, and an instruct model ends its answer with EOS
        if (next == 1 || next == 2){
            stop = "eos";
            break;
        }
        generated += tokenizer.Decode(token,next);
        generated_count++;
        token = next;
    }
    double gen_seconds = SecondsSince(gen_start);
    return json{
        {"text",PrintableText(generated)},
        {"stop",stop},
        {"prompt_tokens",(int)tokens.size()},
        {"generated_tokens",generated_count},
        {"prompt_tokens_per_second",tokens.size() > 1 ? (tokens.size() - 1) / std::max(prompt_seconds,1e-9) : 0.0},
        {"generated_tokens_per_second",generated_count / std::max(gen_seconds,1e-9)},
    };
}

//--- loss ---------------------------------------------------------------------------------------

//-log of the probability the model gives to target, i.e. -log(softmax(logits)[target])
static double TokenLoss(const float* logits, int n, int target){
    float max_val = *std::max_element(logits,logits + n);
    double sum = 0.0;
    for (int i = 0;i < n;i++){ sum += exp(logits[i] - max_val); }
    return log(sum) + max_val - logits[target];
}

//Which characters to score: in a chat transcript only the model's own replies, i.e. the text
//after each [/INST] up to the next [INST]; in a text without [/INST], everything.
static std::vector<uint8_t> ReplyMask(const std::string& doc){
    std::vector<uint8_t> mask(doc.size());
    bool f_chat = doc.find("[/INST]") != std::string::npos;
    bool in_reply = !f_chat;
    for (size_t i = 0;i < doc.size();i++){
        if (f_chat && doc.compare(i,6,"[INST]") == 0){ in_reply = false; }
        if (f_chat && i >= 7 && doc.compare(i - 7,7,"[/INST]") == 0){ in_reply = true; }
        mask[i] = in_reply;
    }
    return mask;
}

json LLMLoss(LLMModel& model, LLMTokenizer& tokenizer, const std::string& text){
    //documents are separated by lines that are exactly "---", each one is scored from position 0
    std::vector<std::string> docs;
    size_t doc_start = 0;
    for (size_t line = 0;line <= text.size();){
        size_t eol = text.find('\n',line);
        size_t line_end = eol == std::string::npos ? text.size() : eol;
        std::string l = text.substr(line,line_end - line);
        if (!l.empty() && l.back() == '\r'){ l.pop_back(); }
        if (l == "---"){
            docs.push_back(text.substr(doc_start,line - doc_start));
            doc_start = eol == std::string::npos ? text.size() : eol + 1;
        }
        if (eol == std::string::npos){ break; }
        line = eol + 1;
    }
    docs.push_back(text.substr(doc_start));

    json doc_results = json::array();
    double total_loss = 0.0;
    long total_scored = 0, total_tokens = 0, forwards = 0;
    auto start = Clock::now();
    for (std::string doc:docs){
        doc = Trim(doc);
        if (doc.empty()){
            continue;
        }
        std::vector<int> tokens = tokenizer.Encode(doc,true,false);
        int n_tokens = (int)tokens.size();
        bool f_truncated = n_tokens > model.context_len;
        if (f_truncated){
            n_tokens = model.context_len;
        }
        //a token is scored if its first character is: decode the tokens back into text to find
        //where each one starts. If that does not line up, score them all
        std::vector<uint8_t> mask = ReplyMask(doc);
        std::vector<uint8_t> scored(n_tokens,0);
        size_t offset = 0;
        bool f_aligned = true;
        for (int i = 1;i < n_tokens && f_aligned;i++){
            std::string piece = tokenizer.Decode(tokens[i - 1],tokens[i]);
            f_aligned = offset + piece.size() <= doc.size() && doc.compare(offset,piece.size(),piece) == 0;
            scored[i] = offset < doc.size() ? mask[offset] : 0;
            offset += piece.size();
        }
        if (!f_aligned){
            std::fill(scored.begin() + 1,scored.end(),1);
        }
        int last = 0;
        for (int i = 1;i < n_tokens;i++){ if (scored[i]){ last = i; } }

        //in batches; the logits at pos predict the token at pos + 1, and a batch with nothing to
        //score skips the classifier for all but its last token
        double doc_loss = 0.0;
        int doc_scored = 0;
        for (int pos = 0;pos < last;pos += MAX_BATCH){
            int nb = std::min(MAX_BATCH,last - pos);
            bool f_any = false;
            for (int b = 0;b < nb;b++){ f_any = f_any || scored[pos + b + 1]; }
            const float* logits = model.ForwardBatch(tokens.data() + pos,nb,pos,0,f_any);
            forwards += nb;
            for (int b = 0;b < nb && f_any;b++){
                if (scored[pos + b + 1]){
                    doc_loss += TokenLoss(logits + (size_t)b * model.config.vocab_size,model.config.vocab_size,tokens[pos + b + 1]);
                    doc_scored++;
                }
            }
        }
        json d = json{ {"tokens",n_tokens},{"scored",doc_scored},{"loss",doc_scored > 0 ? doc_loss / doc_scored : 0.0} };
        if (f_truncated){ d["truncated"] = true; }
        if (!f_aligned){ d["note"] = "tokens did not line up with the text, all scored"; }
        doc_results.push_back(d);
        total_loss += doc_loss;
        total_scored += doc_scored;
        total_tokens += n_tokens;
    }
    double seconds = SecondsSince(start);
    if (total_scored == 0){
        return json{ {"error","nothing to score"} };
    }
    double mean = total_loss / total_scored;
    return json{ {"loss",mean},{"perplexity",exp(mean)},{"scored",total_scored},{"tokens",total_tokens},
                 {"docs",doc_results},{"tokens_per_second",forwards / std::max(seconds,1e-9)} };
}

//--- duet ---------------------------------------------------------------------------------------
//Two instances of the model talk to each other: the same weights, a context each, and each its
//own side of the conversation in the model's instruct format - its own lines are the assistant
//turns, the other's arrive as user turns, framed as <name> says: "<line>". See runq's duet().

#define CALL_TOKENS_FACTOR 4 //a tool call may take this many times the max_tokens of a line

struct DuetAgent{
    int context = 0;
    std::string name, system_prompt, tools;
    int pos = 0;
};

struct Duet{
    LLMModel& model;
    LLMTokenizer& tokenizer;
    LLMSampler& sampler;
    int max_tokens;
    long forwards = 0;

    const float* Feed(DuetAgent& a, int token){
        forwards++;
        return model.Forward(token,a.pos++,a.context);
    }

    /*
        Feed a user turn into the agent and return the logits for its reply, or NULL if its context
        is full. The turn holds the other's line if there is one, then the note if there is one.
        The first turn also carries the system prompt and the tools. It starts with BOS in the
        first turn, and later with the EOS that closes the agent's own previous line.
    */
    const float* Hear(DuetAgent& a, const std::string& speaker, const std::string* line, const std::string* note){
        std::string heard;
        if (line){ heard += speaker + " says: \"" + *line + "\""; }
        if (note){ heard += (line ? "\n\n" : "") + *note; }
        std::string msg;
        if (tokenizer.f_tekken){
            //Ministral 3: the character is the system prompt, then the tools, no spaces around the
            //control tokens. The one who speaks first hears that the other one is there.
            if (a.pos == 0){
                msg += "[SYSTEM_PROMPT]" + a.system_prompt + "[/SYSTEM_PROMPT]";
                if (!a.tools.empty()){ msg += "[AVAILABLE_TOOLS]" + a.tools + "[/AVAILABLE_TOOLS]"; }
            }
            msg += heard.empty() ? "[INST]" + speaker + " is here.[/INST]" : "[INST]" + heard + "[/INST]";
        }else{
            //Mistral 7B: the character is in the first [INST]; v0.3 has control tokens and no space
            //before [/INST], v0.2 writes [INST] out as text
            std::string close = tokenizer.tool_calls >= 0 ? "[/INST]" : " [/INST]";
            if (a.pos == 0 && !a.tools.empty()){ msg += "[AVAILABLE_TOOLS] " + a.tools + "[/AVAILABLE_TOOLS]"; }
            if (heard.empty()){
                msg += "[INST] " + a.system_prompt + close;
            }else if (a.pos == 0){
                msg += "[INST] " + a.system_prompt + "\n\n" + heard + close;
            }else{
                msg += "[INST] " + heard + close;
            }
        }
        std::vector<int> tokens = tokenizer.Encode(msg,false,false);
        tokens.insert(tokens.begin(),a.pos == 0 ? 1 : 2);
        if (a.pos + (int)tokens.size() >= model.context_len){
            return NULL;
        }
        return FeedAll(a,tokens);
    }

    //A turn's tokens in batches, the prompt path; returns the last one's logits.
    const float* FeedAll(DuetAgent& a, const std::vector<int>& tokens){
        const float* logits = model.ForwardTokens(tokens.data(),(int)tokens.size(),a.pos,a.context);
        a.pos += (int)tokens.size();
        forwards += (long)tokens.size();
        return logits;
    }

    //Feed a given line into the agent as its own reply, e.g. the opening line.
    bool Say(DuetAgent& a, const std::string& line){
        std::vector<int> tokens = tokenizer.Encode(line,false,false);
        if (a.pos + (int)tokens.size() >= model.context_len){
            return false;
        }
        FeedAll(a,tokens);
        return true;
    }

    /*
        Sample the agent's line until it ends its turn with EOS, runs into max_tokens or fills its
        context, and return it without surrounding whitespace and without a leading "<name>:",
        which models like to start with. A tool call ([TOOL_CALLS]...) is collected in call
        instead, with a budget of its own, and then this returns true. first >= 0 forces the
        first token, e.g. [TOOL_CALLS] to make the agent call a tool.
        The EOS is not fed here; Hear does that at the start of the agent's next turn.
    */
    bool Speak(DuetAgent& a, const float* logits, int first, std::string& line, std::string& call){
        line.clear();
        call.clear();
        bool f_checked = false, f_calling = false;
        int token = 0;
        int limit = max_tokens;
        for (int n = 0;n < limit && a.pos < model.context_len && logits;n++){
            int next = first >= 0 ? first : sampler.Sample(logits);
            model.RecordBlocks(next);
            first = -1;
            if (next == 2){ break; }
            std::string piece = tokenizer.Decode(token,next);
            if (!f_calling && next == tokenizer.tool_calls){
                f_calling = true;
                n = 0;
                limit = CALL_TOKENS_FACTOR * max_tokens;
            }
            if (f_calling){
                call += piece;
            }else{
                line += piece;
                if (!f_checked){
                    size_t skip = 0;
                    while (skip < line.size() && isspace((unsigned char)line[skip])){ skip++; }
                    if (line.size() - skip > a.name.size() && line.compare(skip,a.name.size(),a.name) == 0 && line[skip + a.name.size()] == ':'){
                        skip += a.name.size() + 1;
                        while (skip < line.size() && isspace((unsigned char)line[skip])){ skip++; }
                    }
                    line.erase(0,skip);
                    f_checked = line.size() > a.name.size() || line.compare(0,line.size(),a.name,0,line.size()) != 0;
                }
            }
            logits = Feed(a,next);
            token = next;
        }
        line = Trim(PrintableText(line));
        return f_calling;
    }
};

//call holds one or more [TOOL_CALLS]name[ARGS]{json} (Ministral 3), or [TOOL_CALLS] [{...}] (Mistral 7B v0.3)
static std::string ToolCallsText(const std::string& call, json& calls){
    std::string out;
    const std::string marker = "[TOOL_CALLS]";
    size_t c = call.find(marker);
    while (c != std::string::npos){
        c += marker.size();
        while (c < call.size() && call[c] == ' '){ c++; }
        size_t next = call.find(marker,c);
        size_t end = next == std::string::npos ? call.size() : next;
        size_t args = call.find("[ARGS]",c);
        std::string name, arguments;
        if (args != std::string::npos && args < end){
            name = call.substr(c,args - c);
            arguments = call.substr(args + 6,end - args - 6);
            out += "-> " + name + " " + arguments + "\n";
        }else{
            arguments = call.substr(c,end - c);
            out += "-> " + arguments + "\n";
        }
        calls.push_back(json{ {"name",name},{"arguments",arguments} });
        c = next;
    }
    return out;
}

json LLMDuet(LLMModel& model, LLMTokenizer& tokenizer, const LLMDuetArgs& args){
    if (model.n_contexts < 2){
        return json{ {"error","a duet needs two contexts; load the model with contexts >= 2"} };
    }
    std::string tools = args.tools;
    if (!tools.empty() && tokenizer.tool_calls < 0){
        tools.clear();  //the model has no tool calls; runq says so and carries on without them
    }
    std::string sections[2], agent_tools[2];
    SplitAtDashes(args.scenario,sections[0],sections[1]);
    SplitAtDashes(tools,agent_tools[0],agent_tools[1]);
    if (Trim(agent_tools[1]).empty()){ agent_tools[1] = agent_tools[0]; }

    DuetAgent agents[2];
    for (int i = 0;i < 2;i++){
        std::string s = Trim(sections[i]);
        size_t eol = s.find('\n');
        if (eol == std::string::npos){
            return json{ {"error","the scenario should hold two characters separated by a line \"---\", each one a line "
                                  "with its name followed by its system prompt"} };
        }
        agents[i].context = i;
        agents[i].name = Trim(s.substr(0,eol));
        agents[i].system_prompt = Trim(s.substr(eol + 1));
        agents[i].tools = Trim(agent_tools[i]);
    }

    LLMSampler sampler(model.config.vocab_size,args.temperature,args.topp,args.seed);
    Duet duet{model,tokenizer,sampler,args.max_tokens};
    std::string transcript, line, call;
    json calls = json::array();
    bool called[2] = {false,false};
    int turn = 0;
    std::string ended = "lines";
    auto start = Clock::now();
    for (;turn < args.lines;turn++){
        DuetAgent& a = agents[turn % 2];
        DuetAgent& other = agents[(turn + 1) % 2];
        const float* logits = duet.Hear(a,other.name,turn == 0 ? NULL : &line,NULL);
        if (!logits){
            ended = "the context of " + a.name + " is full";
            break;
        }
        transcript += a.name + ": ";
        if (turn == 0 && !args.opening.empty()){
            if (!duet.Say(a,args.opening)){
                return json{ {"error","the opening line does not fit in the context"} };
            }
            line = args.opening;
        }else{
            called[turn % 2] = duet.Speak(a,logits,-1,line,call);
        }
        if (called[turn % 2]){
            //a tool call ends the conversation
            if (!line.empty()){ transcript += line + "\n"; }
            size_t before = calls.size();
            transcript += ToolCallsText(call,calls);
            for (size_t c = before;c < calls.size();c++){ calls[c]["by"] = a.name; }
            ended = "tool call";
            turn++;
            break;
        }
        transcript += line + "\n\n";
    }
    //with tools, each one who hasn't called one yet ends with a tool call: first the one whose
    //turn it is, who still has to hear the last line, then the one who said it
    int tool_calls = tools.empty() ? -1 : tokenizer.tool_calls;
    const std::string over = "The conversation is over.";
    for (int k = 0;k < 2 && tool_calls >= 0;k++){
        int i = (turn + k) % 2;
        if (called[i]){ continue; }
        DuetAgent& a = agents[i];
        DuetAgent& other = agents[1 - i];
        const float* logits = duet.Hear(a,other.name,k == 0 && !line.empty() ? &line : NULL,&over);
        if (!logits){
            transcript += "(the context of " + a.name + " is full, it can't call a tool)\n";
            continue;
        }
        transcript += a.name + ": ";
        duet.Speak(a,logits,tool_calls,line,call);
        size_t before = calls.size();
        transcript += ToolCallsText(call,calls);
        for (size_t c = before;c < calls.size();c++){ calls[c]["by"] = a.name; }
        line.clear();
    }
    double seconds = SecondsSince(start);
    return json{ {"transcript",transcript},{"calls",calls},{"ended",ended},{"forwards",duet.forwards},
                 {"seconds",seconds},{"tokens_per_second",duet.forwards / std::max(seconds,1e-9)} };
}

//--- chat ---------------------------------------------------------------------------------------

void LLMChatSession::Reset(void){
    messages.clear();
    tokens.clear();
    f_cache_valid = false;
    logits = NULL;
    f_generating = false;
    sampler.reset();
    generated = 0;
}

bool LLMChatSession::Feed(LLMModel& model, const std::vector<int>& new_tokens){
    auto start = Clock::now();
    int ctx = Context(model);
    if (!f_cache_valid){
        //someone else used this context since: feed the whole conversation again
        std::vector<int> all = tokens;
        all.insert(all.end(),new_tokens.begin(),new_tokens.end());
        logits = model.ForwardTokens(all.data(),(int)all.size(),0,ctx);
        prompt_tokens = (int)all.size();
    }else{
        logits = model.ForwardTokens(new_tokens.data(),(int)new_tokens.size(),(int)tokens.size(),ctx);
        prompt_tokens = (int)new_tokens.size();
    }
    prompt_seconds = SecondsSince(start);
    tokens.insert(tokens.end(),new_tokens.begin(),new_tokens.end());
    f_cache_valid = logits != NULL;
    return f_cache_valid;
}

bool LLMChatSession::Send(LLMModel& model, LLMTokenizer& tokenizer, const std::string& text, std::string& error){
    if (f_generating){
        error = "still answering";
        return false;
    }
    bool f_first = tokens.empty();
    std::string msg;
    if (tokenizer.f_tekken){
        if (f_first && !system_prompt.empty()){
            msg += "[SYSTEM_PROMPT]" + system_prompt + "[/SYSTEM_PROMPT]";
        }
        msg += "[INST]" + text + "[/INST]";
    }else{
        //Mistral 7B: the system prompt goes in the first [INST]; v0.3 has no space before [/INST]
        std::string close = tokenizer.tool_calls >= 0 ? "[/INST]" : " [/INST]";
        std::string body = f_first && !system_prompt.empty() ? system_prompt + "\n\n" + text : text;
        msg += "[INST] " + body + close;
    }
    std::vector<int> new_tokens = tokenizer.Encode(msg,false,false);
    //BOS starts the conversation; later the EOS that closes the assistant's previous reply
    new_tokens.insert(new_tokens.begin(),f_first ? 1 : 2);
    if ((int)(tokens.size() + new_tokens.size()) + 1 >= model.context_len){
        error = "the conversation is full (" + std::to_string(tokens.size()) + " of " +
                std::to_string(model.context_len) + " positions) - start a new one";
        return false;
    }
    if (!Feed(model,new_tokens)){
        error = "the forward pass refused the turn";
        return false;
    }
    messages.push_back({"user",text});
    messages.push_back({"assistant",""});
    sampler.reset(new LLMSampler(model.config.vocab_size,temperature,topp,seed++));
    generated = 0;
    prev_token = new_tokens.back();
    reply_start = Clock::now();
    tokens_per_second = 0.0;
    f_generating = true;
    return true;
}

void LLMChatSession::Step(LLMModel& model, LLMTokenizer& tokenizer, double budget_ms){
    if (!f_generating){
        return;
    }
    auto start = Clock::now();
    if (!f_cache_valid){
        //a job used the context mid-reply: feed everything so far again, then carry on
        if (!Feed(model,{})){
            f_generating = false;
            return;
        }
    }
    int ctx = Context(model);
    while (std::chrono::duration<double,std::milli>(Clock::now() - start).count() < budget_ms){
        int next = sampler->Sample(logits);
        model.RecordBlocks(next);   //before the next Forward overwrites what it records
        //EOS ends the reply; it is fed with the next user turn, not now
        if (next == 2 || generated >= max_tokens || (int)tokens.size() + 1 >= model.context_len){
            f_generating = false;
            break;
        }
        messages.back().text += PrintableText(tokenizer.Decode(prev_token,next));
        logits = model.Forward(next,(int)tokens.size(),ctx);
        tokens.push_back(next);
        prev_token = next;
        generated++;
        if (!logits){
            f_generating = false;
            break;
        }
    }
    double seconds = SecondsSince(reply_start);
    tokens_per_second = seconds > 0.0 ? generated / seconds : 0.0;
}
