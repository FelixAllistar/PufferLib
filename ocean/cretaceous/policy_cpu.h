// CPU inference for trained Cretaceous policies (docs/encoder.md, "CPU inference").
//
// Loads a checkpoint the trainer writes (checkpoints/cretaceous/<run>/<step>.bin: the fp32 master weights, tensors
// back to back in registration order) for either encoder and runs one step per agent: observation in, encoder,
// MinGRU layers, decoder, action out. The recurrent state is kept per agent slot. Plain C, no BLAS; the dense layers
// split their output rows over OpenMP threads, which never changes a result, so a run is deterministic.
//
// File layout (src/algo.cu weights_create: encoder, decoder, network):
//   cretaceous_net 0: encoder (hidden x 243), no bias, no activation
//   cretaceous_net 1: the 11 tensors of cretaceous_encoder.cu (species .. trunk2), each (out x in incl. bias column)
//   decoder ((28 logits + 1 value) x hidden)
//   MinGRU layer l: (3 hidden x hidden) for l = 0 .. layers - 1
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sim.h"

#define CRET_POLICY_ATN 28                       // sum of CRET_ACTION_SIZES: the decoder's logits
#define CRET_POLICY_OUT (CRET_POLICY_ATN + 1)    // logits + value

typedef struct CretPolicy CretPolicy;

// Floats a checkpoint of this contract holds for an encoder (0 linear, 1 structured), hidden size and layer count.
long cret_policy_param_count(int net, int hidden, int layers);

// Builds a policy over a copy of n weights for up to max_agents slots. net, hidden or layers below 0 are found from
// n when exactly one (net, hidden, layers) fits it. Returns NULL and writes why to err (both sizes) on a mismatch.
CretPolicy* cret_policy_create(const float* weights, long n, int net, int hidden, int layers, int max_agents,
    char* err, size_t errlen);
// Reads a checkpoint file and calls cret_policy_create.
CretPolicy* cret_policy_load(const char* path, int net, int hidden, int layers, int max_agents, char* err, size_t errlen);
void cret_policy_free(CretPolicy* p);
void cret_policy_describe(const CretPolicy* p, char* out, size_t len);   // "cretaceous_net 1, hidden 256 x 2 layers"

// Clears one slot's recurrent state (a new life), or every slot's with slot -1.
void cret_policy_reset(CretPolicy* p, int slot);
// One step for n agents: obs holds n rows of CRET_OBS_SIZE floats, row k belongs to slot slots[k] (each slot at
// most once). Updates those slots' recurrent state; the outputs stay readable until the next forward.
void cret_policy_forward(CretPolicy* p, const float* obs, const int* slots, int n);
const float* cret_policy_output(const CretPolicy* p, int k);    // row k: 28 logits, then the value
const float* cret_policy_encoded(const CretPolicy* p, int k);   // row k: the encoder's output (hidden floats)
// Picks the action of row k into actions[CRET_ACTION_HEADS], one choice per head: sampled from the softmax with the
// sim's xorshift generator on *rng (as the trainer samples), or the first largest logit when greedy.
void cret_policy_pick(const CretPolicy* p, int k, bool greedy, uint32_t* rng, float* actions);

// Drives the agents with mask[i] set: a slot whose animal died or started a new life gets a fresh state (as the
// trainer clears it on terminals), a dead animal gets no input, and the living ones are observed, stepped as one
// batch in slot order and decoded into in[i]. Other slots are left alone.
void cret_policy_act(CretPolicy* p, const CretSim* s, const bool* mask, bool greedy, uint32_t* rng, CretInput* in);

// --policy arguments for the player and the tools: "[WHO=]PATH[:NET:HIDDEN:LAYERS]". WHO is all (the default),
// herbivores, carnivores, a species key (raptor, stego, trex, pachy, sauropelta, galli) or slotN.
typedef struct CretPolicyArg { char who[32]; char path[512]; int net, hidden, layers; } CretPolicyArg;
bool cret_policy_parse_arg(const char* arg, CretPolicyArg* out);
bool cret_policy_who_valid(const char* who);
bool cret_policy_who_matches(const char* who, int slot, int species);
