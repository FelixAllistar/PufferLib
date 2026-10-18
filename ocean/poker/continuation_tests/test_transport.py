"""Frozen new recovery workload. Synthetic checks are not opponent evidence."""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
from continuation_transport import (Context, Session, Public, ProtocolError,
    _rebuild, public_from_native, bind_library)


def load_native(path):
    spec = importlib.util.spec_from_file_location("frozen_poker_eval", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module.Native


def text_card(card):
    return "23456789TJQKA"[card // 4] + "cdhs"[card % 4]


def response_for(truth, hero, history, terminal=False):
    state, rows, board = truth.snapshot()
    count = state[1]+2 if state[1] else 0
    return {"client_pos": hero, "hole_cards": [text_card(c) for c in rows[hero][-2:]],
            "board": [text_card(c) for c in board[:count]], "action": history, "terminal": terminal}


def append_action(history, before, action, after):
    actor = before.actor
    token = "f" if action < 0 else f"b{action}" if action else (
        "c" if before.high > getattr(before, f"bet{actor}") else "k")
    history += token
    if after.actor != 255 and after.street != before.street:
        history += "/"
    elif after.actor == 255 and after.live == 3 and before.street < 3:
        history += "/" * (3-before.street)
    return history


def pure_after(lib, before, action, truth):
    state, _, board = truth.snapshot()
    count = state[1]+2 if state[1] else 0
    after = Public()
    assert lib.pc_public_next(C.byref(before), action, (C.c_int64*5)(*board), count, C.byref(after))
    return after


def independent_settlement(deck,history):
    """Independent integer contribution replay + eval7 rank, after close only.

    The validated action string supplies street raise-to totals; this function
    reads no native total/stack/rank or PcPublic transition to compute payouts.
    """
    import eval7
    total=[100,50]
    bets=[100,50]
    high,actor=100,1
    folder=None
    for token in re.findall(r"b[0-9]+|[kcf/]",history):
        if token=="/":
            bets=[0,0];high=0;actor=0
            continue
        if token=="f":
            folder=actor
            break
        target=int(token[1:]) if token.startswith("b") else high
        pay=min(target-bets[actor],20000-total[actor])
        assert pay>=0
        bets[actor]+=pay;total[actor]+=pay
        if token.startswith("b"):
            high=target
        actor=1-actor
    if folder is not None:
        expected=[20000-total[0],20000-total[1]]
        expected[1-folder]+=sum(total)
        return {"method":"independent integer contributions, fold transfer", "stacks":expected,"total":total}
    ranks=[eval7.evaluate([eval7.Card(text_card(c)) for c in deck[2*p:2*p+2]+deck[4:]]) for p in (0,1)]
    sign=(ranks[0]>ranks[1])-(ranks[0]<ranks[1])
    gain=sign*min(total)
    return {"method":"independent integer contributions and eval7 showdown ranking",
            "stacks":[20000+gain,20000-gain],"total":total,"eval7_ranks":ranks}


def play(Native, candidate, control, deck, hand, hero, mode):
    # Fixed seed identities never depend on actions, winner or runtime.
    gen = 1000 + 2*hand + hero
    policy_seed = 202610185501 + 1009*hand + 101*hero
    opponent_seed = 202610185503 + 1013*hand + 103*hero
    truth = Native(players=2, seed=policy_seed, library=candidate)
    policy = truth if mode == "native_incremental" else Native(players=2, seed=policy_seed, library=candidate)
    opponent = Native(players=2, seed=opponent_seed, library=control)
    bind_library(truth.lib)
    context = Context(policy, f"synthetic/jobA/hand{hand}/seat{hero}", hero)
    context.begin(gen)  # Exactly before the true deal/reset.
    truth.reset([20000,20000], deck, button=1)
    session = Session(context) if mode == "full_prefix_rebuild" else None
    own = tuple(sorted(deck[2*hero:2*hero+2]))
    if session is None:
        context.own(own)
        context.start(public_from_native(truth))
    history = ""
    trace = []
    request = 0
    terminal = False
    for _ in range(4096):
        before = public_from_native(truth)
        if before.actor == hero:
            request += 1
            if session:
                message = response_for(truth,hero,history)
                status = session.ingest(gen,message)
                assert not status["terminal"]
                audit = context.audit()
                repeated = session.ingest(gen,message)
                assert repeated["new_events"] == 0 and context.audit() == audit
            audit_before = context.audit()
            rng_before = policy.rng.value
            action = session.decide(request) if session else context.decide(request)
            audit_after = context.audit()
            rng_after = policy.rng.value
            duplicate = session.decide(request) if session else context.decide(request)
            assert duplicate == action and policy.rng.value == rng_after and context.audit() == audit_after
            trace.append({"before": bytes(before).hex(), "action": action,
                          "rng_before": rng_before, "rng_after": rng_after,
                          "audit_before": audit_before, "audit_after": audit_after})
        else:
            C.memmove(opponent.storage, truth.storage, len(truth.storage))
            action = opponent.bot(7)
        truth.step(action)
        after = pure_after(truth.lib,before,action,truth)
        history = append_action(history,before,action,after)
        if session is None:
            context.own(own)  # Includes the terminal transition.
            context.observe(before,action,after)
        terminal = after.actor == 255
        if terminal:
            if session:
                message = response_for(truth,hero,history,True)
                status = session.ingest(gen,message)
                assert status["terminal"] and status["closed"]
                audit = context.audit()
                # Entire duplicate terminal prefix and own holding are checked.
                duplicate = session.ingest(gen,message)
                assert duplicate["new_events"] == 0 and context.audit() == audit
                official = dict(message)
                _, rows, _ = truth.snapshot()
                official["winnings"] = rows[hero][0] - 20000
                official["slumbot_hole_cards"] = [text_card(c) for c in rows[1-hero][-2:]]
                proof = session.settle_revealed(gen,official)
                assert proof["verified"] and proof["winnings_chips"] == official["winnings"]
            else:
                context.end(1)
            break
    assert terminal, "fixed hand exceeded its frozen decision budget"
    state, rows, _ = truth.snapshot()
    assert context.closed and context.audit()["phase"]==4
    independent=independent_settlement(deck,history)
    assert independent["stacks"]==[r[0] for r in rows]
    assert independent["total"]==[r[2] for r in rows]
    result = {"hand":hand,"hero":hero,"mode":mode,"history":history,"trace":trace,
              "terminal_audit":context.audit(),"synthetic_terminal_stacks":[r[0] for r in rows],
              "terminal_decisions":state[9],"opponent_rng":opponent.rng.value,
              "independent_settlement":independent}
    # Every full-prefix hand also proves wrong own cards cannot use a closed
    # terminal receipt. A valid alternate card is picked mechanically from a
    # fixed ordered deck; this is a malformed-message test, not a policy trial.
    if session:
        bad = dict(message)
        occupied = set(own) | set(deck[4:])
        replacement = next(c for c in range(52) if c not in occupied)
        bad["hole_cards"] = [text_card(own[0]),text_card(replacement)]
        try:
            session.ingest(gen,bad)
        except ProtocolError:
            assert context.audit()["phase"] == 5
        else:
            raise AssertionError("changed terminal own pair was accepted")
    context.close()
    return result


def grammar(Native,candidate):
    native=Native(players=2,seed=31,library=candidate)
    bind_library(native.lib)
    own=(48,49)
    full=(4,17,30,43,46)
    good=[("",0),("c",0),("ck",3),("ck/",3),("ck/kk",4),("ck/kk/",4),
          ("ck/kk/kk",5),("ck/kk/kk/",5),("ck/kk/kk/kk",5),
          ("b20000c",5),("b20000c///",5),("ck/b19900c//",5),
          ("ck/kk/b19900c/",5),("b200f",0),("ck/kb100f",3)]
    accepted={}
    for history,count in good:
        _,_,state,events=_rebuild(native,1,own,full[:count],history)
        accepted[history]=(state.actor,events)
    for a,b in [("ck","ck/"),("ck/kk","ck/kk/"),("ck/kk/kk","ck/kk/kk/"),("b20000c","b20000c///")]:
        assert accepted[a] == accepted[b]
    bad=["/","k","cc","ckkk","ck//","ck/kkk","b","b0","b-1","b2147483648",
         "b20001","b20000c/","b20000c//","b20000c////","ck/kk/kk/kk/",
         "b200f/","ck/kf","ck/ck","ck/b50"]
    for history in bad:
        try:
            _rebuild(native,1,own,full,history)
        except ProtocolError as exc:
            assert "visible board does not match" not in str(exc),history
        else:
            raise AssertionError(f"malformed grammar accepted: {history}")
    return {"accepted":len(good),"rejected":len(bad),"semantic_alias_pairs":4}


def independent_grammar(Native,candidate):
    path=ROOT/"fixtures"/"grammar_corpus.json"
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    assert digest=="aa291e838d381e31edd031465421bcd95a670f6c8350e6fcd0504f31a8810933"
    corpus=json.loads(path.read_text())
    native=Native(players=2,seed=37,library=candidate)
    bind_library(native.lib)
    counts={True:0,False:0}
    for case in corpus["cases"]:
        count=case.get("board_count",5)
        try:
            _,_,state,_=_rebuild(native,corpus["hero_seat"],tuple(corpus["own_cards"]),
                tuple(corpus["base_board"][:count]),case["history"])
        except ProtocolError as exc:
            assert not case["valid"],(case["id"],str(exc))
            assert "visible board does not match" not in str(exc),case["id"]
        else:
            assert case["valid"],case["id"]
            assert state.street==case["street"] and state.actor==case["actor"],case["id"]
        counts[case["valid"]]+=1
    return {"corpus_sha256":digest,"accepted":counts[True],"rejected":counts[False]}


def lifecycle_guards(Native,candidate):
    cases=[]
    def setup(index):
        n=Native(players=2,seed=71,library=candidate)
        c=Context(n,f"guard/worker{index}",1)
        c.begin(41)
        s=Session(c)
        message={"client_pos":1,"hole_cards":["Ac","Ad"],"board":[],"action":"","terminal":False}
        s.ingest(41,message)
        return c,s,message
    mutations=[("bool_request",lambda c,s,m:s.decide(True)),
               ("zero_request",lambda c,s,m:s.decide(0)),
               ("overflow_request",lambda c,s,m:s.decide(1<<64)),
               ("old_generation",lambda c,s,m:s.ingest(40,m)),
               ("bool_generation",lambda c,s,m:s.ingest(True,m)),
               ("changed_own",lambda c,s,m:s.ingest(41,dict(m,hole_cards=["Ac","Kd"]))),
               ("bool_terminal",lambda c,s,m:s.ingest(41,dict(m,terminal=0))),
               ("conflicting_seat",lambda c,s,m:s.ingest(41,dict(m,position=0))),
               ("own_collision",lambda c,s,m:s.ingest(41,dict(m,board=["Ac","2d","3h"]))),
               ("bad_end",lambda c,s,m:c.end(True)),
               ("bad_own",lambda c,s,m:c.own([True,12])),
               ("repeated_begin",lambda c,s,m:c.begin(41))]
    for i,(name,operation) in enumerate(mutations):
        c,s,message=setup(i)
        try:
            operation(c,s,message)
        except (ProtocolError,TypeError):
            assert c.audit()["phase"] == 5,name
        else:
            raise AssertionError(f"guard accepted: {name}")
        c.close();cases.append(name)
    class Poison(dict):
        def get(self,*args):
            raise AssertionError("response parsed before envelope")
        def __contains__(self,*args):
            raise AssertionError("response parsed before envelope")
    c,s,_=setup(98)
    try:
        s.ingest(40,Poison())
    except ProtocolError:
        assert c.audit()["phase"]==5
    else:
        raise AssertionError("old identical envelope accepted")
    c.close();cases.append("generation_before_response_parse")
    c,s,message=setup(99)
    s.decide(1)
    s.abandon()
    assert c.closed and c.audit()["phase"]==4
    c.begin(42)
    fresh=Session(c)
    fresh.ingest(42,message)
    assert c.audit()["generation"]==42 and c.audit()["anchors"]==0
    c.close();cases.append("abandon_then_strict_new_generation")
    return cases


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--candidate",required=True)
    parser.add_argument("--control",required=True)
    parser.add_argument("--eval-source",required=True)
    parser.add_argument("--output",required=True)
    args=parser.parse_args()
    Native=load_native(args.eval_source)
    deck_path=ROOT/"fixtures"/"synthetic_decks.json"
    deck_hash=hashlib.sha256(deck_path.read_bytes()).hexdigest()
    assert deck_hash=="82577847ce10ce4ca294e0065ef12cbff09d61216027960bdb75025d4b3eff5f"
    decks=json.loads(deck_path.read_text())["decks"]
    assert len(decks)==16 and all(len(d)==9 and len(set(d))==9 and all(type(c) is int and 0<=c<52 for c in d) for d in decks)
    results=[]
    for hand,deck in enumerate(decks):
        for hero in (0,1):
            pair=[play(Native,args.candidate,args.control,deck,hand,hero,mode)
                  for mode in ("native_incremental","full_prefix_rebuild")]
            a,b=pair
            for key in ("history","trace","terminal_audit","synthetic_terminal_stacks","terminal_decisions","opponent_rng","independent_settlement"):
                assert a[key]==b[key],(hand,hero,key)
            results.extend(pair)
            print(json.dumps({"paired_hand":hand,"hero":hero,"actions":a["terminal_decisions"],
                              "accepted_anchors":a["terminal_audit"]["anchors"]}),flush=True)
    report={"schema":"continuation-recovery-transport-proof-v1","status":"passed",
            "scope":"64 fixed synthetic actual-native hand executions, not playing-strength evidence",
            "candidate_sha256":hashlib.sha256(Path(args.candidate).read_bytes()).hexdigest(),
            "control_sha256":hashlib.sha256(Path(args.control).read_bytes()).hexdigest(),
            "eval_source_sha256":hashlib.sha256(Path(args.eval_source).read_bytes()).hexdigest(),
            "test_source_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "deck_seed":202610185501,"deck_manifest_sha256":deck_hash,"decks":decks,"executions":len(results),
            "grammar":grammar(Native,args.candidate),"independent_grammar":independent_grammar(Native,args.candidate),
            "lifecycle_guards":lifecycle_guards(Native,args.candidate),
            "results":results}
    Path(args.output).write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
    print(json.dumps({"status":report["status"],"executions":len(results),"grammar":report["grammar"],
                      "lifecycle_guards":len(report["lifecycle_guards"])}),flush=True)


if __name__=="__main__":
    main()
