"""Create the additive Puffer header from exact a4b source text only.

This module performs no compilation, native import, model read or HTTP call.
The returned bytes must match the source-reviewed hook header exactly.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

SOURCE_SHA256 = "61c189abb8e89de05b5e2f6fe339f1a718c83f27a88bd065fbe8dcbae2677c3a"
GENERATED_SHA256 = "035c34996dadc9fe86bafd6d6149f9bb49854a7def2cf8f61f6dacccb8a24779"


configuration=r'''
#if POKER_CLONE_BRIDGE && POKER_PLAYERS == 2
/* Additive opt-in, called after my_vec_init and BEFORE any first puf_reset.
 * The coordinator supplies a distinct job/worker/vector-instance namespace.
 * This function does not discover a model, open a path or choose a namespace.
 */
void bb_puffer_configure_vector(Env* envs,size_t count,const unsigned char namespace_id[32],
    const BbModelBinding* binding) {
    unsigned char nonzero=0;
    if(namespace_id)for(int i=0;i<32;i++)nonzero|=namespace_id[i];
    if(!envs || !count || count>1000000 || !nonzero || !bb_model_matches(binding)) {
        fprintf(stderr,"source-bound Puffer clone configuration is required\n");abort();
    }
    for(size_t i=0;i<count;i++) {
        Env* env=envs+i;
        if(!env->game || env->clone_bridge.context || env->clone_bridge.generation
            || env->game->pot[env->lane] || env->game->decisions[env->lane]) {
            fprintf(stderr,"Puffer clone must be configured before the first true deal\n");abort();
        }
    }
    for(size_t i=0;i<count;i++) {
        Env* env=envs+i;unsigned char owner[32];BbSha256 hash;
        bb_sha_domain(&hash,"puffer-clone-owner-v1");bb_sha_add(&hash,namespace_id,32);
        bb_sha_u64(&hash,i);bb_sha_u64(&hash,env->rng);bb_sha_u64(&hash,env->lane);bb_sha_finish(&hash,owner);
        BB_PUFFER_CONFIGURE(env,1,owner,binding);
    }
}
#endif

'''

def render(raw):
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError("Puffer source is not the pinned a4b header")
    text = raw.decode("utf-8")
    def once(before,after):
        nonlocal text
        if text.count(before)!=1:raise ValueError("expected one original hook location: "+before)
        text=text.replace(before,after)
    once('#include "core.h"','#include "../reference_a4b/core.h"\n#include "puffer_clone_hooks.h"')
    once("struct Env {\n","struct Env {\n    BB_PUFFER_FIELDS\n")
    once("            action = poker_decide(g, t, &env->policy_rng, env->baseline_styles[p],\n                env->equity_samples, env->profiles);",
         "            action = BB_PUFFER_DECIDE(env, env->baseline_styles[p]);")
    once("        poker_step(g, t, action);","        BB_PUFFER_STEP(env, action);")
    once("        poker_deal(g, t, (g->button[t] + 1) % POKER_PLAYERS, stacks);",
         "        BB_PUFFER_BEFORE_DEAL(env);\n        poker_deal(g, t, (g->button[t] + 1) % POKER_PLAYERS, stacks);\n        BB_PUFFER_AFTER_DEAL(env);")
    once("    poker_step(env->game, env->lane, amounts[action]);","    BB_PUFFER_STEP(env, amounts[action]);")
    once("void puf_close(Env* env) {\n","void puf_close(Env* env) {\n    BB_PUFFER_CLOSE(env);\n")
    once("void poker_field(Env* env) {",configuration+"void poker_field(Env* env) {")
    result = text.encode("utf-8")
    if hashlib.sha256(result).hexdigest() != GENERATED_SHA256:
        raise ValueError("generated Puffer hook bytes differ")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-poker-header", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = render(args.base_poker_header.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("xb") as stream:
        stream.write(result)
    print(json.dumps({"path": str(args.output.resolve()), "sha256": GENERATED_SHA256,
                      "bytes": len(result), "operation": "source text generation only"}))


if __name__ == "__main__":
    main()
