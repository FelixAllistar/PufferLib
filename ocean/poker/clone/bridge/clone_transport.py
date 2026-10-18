"""Prospective explicit clone transport adapter. No HTTP or model discovery.

An execution gate must bind a qualifying v2 module and this complete bridge
before production construction is allowed. Call begin
before the true deal request; pass that captured generation to every ingest.
Full public replay is a separate operation and never starts a new generation.
"""
from __future__ import annotations
import ctypes as C
import hashlib
import json
from pathlib import Path

from clone_public_replay import (Public, ProtocolError, U64_MAX, cards,
    integer, public_from_native, _rebuild, _validate_action)

NEW, RESERVED, ACTIVE, TERMINAL, ABANDONED, ERROR = range(6)

def digest(value, name):
    if type(value) is not str or len(value)!=64 or value=="0"*64 or any(c not in "0123456789abcdef" for c in value):
        raise ProtocolError("invalid " + name + " SHA256")
    return value

class ModelBinding(C.Structure):
    _fields_=[("model_domain",C.c_char*64)]+[(name,C.c_char*65) for name in (
        "model_npz_sha256","model_numeric_sha256","module_source_closure_sha256","module_binary_sha256")]
    _fields_ += [("numeric_bytes",C.c_uint64),("nodes",C.c_uint64)]

class Audit(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ("generation","events","decisions_requested","hits","fallbacks",
        "pending_request","pending_rng_before","pending_rng_after")]
    _fields_ += [(name,C.c_int32) for name in ("phase","pending","pending_seat","pending_action","pending_branch")]
    _fields_ += [("owner",C.c_ubyte*32),("public_prefix_sha256",C.c_ubyte*32)]

def bind_library(lib):
    owner,public=C.POINTER(C.c_ubyte),C.POINTER(Public)
    signatures={
        "bb_create":[owner,C.POINTER(ModelBinding),C.c_uint],"bb_free":[C.c_void_p],
        "bb_begin":[C.c_void_p,owner,C.c_uint64],"bb_envelope":[C.c_void_p,owner,C.c_uint64,C.c_int],
        "bb_bind_native":[C.c_void_p,owner,C.c_uint64,C.c_void_p,C.c_int],
        "bb_start":[C.c_void_p,owner,C.c_uint64,public],
        "bb_observe":[C.c_void_p,owner,C.c_uint64,public,C.c_int32,public],
        "bb_abandon":[C.c_void_p,owner,C.c_uint64],
        "bb_terminal_receipt":[C.c_void_p,owner,C.c_uint64,public],
        "bb_protocol_error":[C.c_void_p,C.c_char_p],"bb_last_error":[C.c_void_p],
        "bb_audit":[C.c_void_p,C.POINTER(Audit)],
        "bb_decide":[C.c_void_p,owner,C.c_uint64,C.c_void_p,C.c_int,C.POINTER(C.c_uint64),
            C.c_int,C.c_int,C.c_void_p,C.c_uint64,C.POINTER(C.c_int32)],
        "bb_public_from_game":[C.c_void_p,C.c_int,public],
        "bb_public_next":[public,C.c_int32,C.POINTER(C.c_int64),C.c_int,public],
        "bb_game_status":[C.c_void_p,C.c_int,C.POINTER(C.c_int64)],
    }
    for name,args in signatures.items():
        function=getattr(lib,name);function.argtypes=args;function.restype=C.c_int
    lib.bb_create.restype=C.c_void_p;lib.bb_free.restype=None;lib.bb_last_error.restype=C.c_char_p

class Native:
    """Private transport core. Its public getter never returns hidden cards.

    Raw replay step is used only on a newly allocated scratch replay engine.
    It never calls a policy, appends to the live context, or resets hand identity.
    """
    def __init__(self, library, expected_sha256, seed=73, samples=0):
        self.path=Path(library).resolve()
        self.expected_sha256=digest(expected_sha256,"bridge binary")
        if hashlib.sha256(self.path.read_bytes()).hexdigest()!=self.expected_sha256:
            raise ProtocolError("bridge binary differs from source-bound gate")
        self.samples=integer(samples,"samples",0,128)
        if self.samples not in (0,32,64,128):raise ProtocolError("invalid sample budget")
        self.players=2;self.lib=C.CDLL(str(self.path));bind_library(self.lib)
        self.lib.poker_bytes.argtypes=[];self.lib.poker_bytes.restype=C.c_size_t
        self.lib.poker_seed.argtypes=[C.c_void_p,C.c_uint64];self.lib.poker_seed.restype=None
        self.lib.poker_reset.argtypes=[C.c_void_p,C.c_int,C.c_int,C.POINTER(C.c_uint32),C.POINTER(C.c_uint8)]
        self.lib.poker_reset.restype=None
        self.lib.poker_step.argtypes=[C.c_void_p,C.c_int,C.c_int32];self.lib.poker_step.restype=None
        self.lib.poker_snapshot.argtypes=[C.c_void_p,C.c_int,C.POINTER(C.c_uint32)];self.lib.poker_snapshot.restype=None
        self.lib.poker_actions.argtypes=[C.c_void_p,C.c_int,C.POINTER(C.c_int32)];self.lib.poker_actions.restype=C.c_uint
        count=integer(self.lib.poker_bytes(),"Poker allocation bytes",1,1<<20)
        self.storage=C.create_string_buffer(count)
        self.rng=C.c_uint64(integer(seed,"policy seed"))
        self.lib.poker_seed(self.storage,seed)

    def fork_replay(self):
        return Native(self.path,self.expected_sha256,seed=1,samples=self.samples)

    def reset(self,stacks,deck,button=1):
        if type(stacks) is not list or len(stacks)!=2 or type(deck) not in (list,tuple) or len(deck)!=9:
            raise ProtocolError("invalid internal replay shape")
        for value in stacks:integer(value,"initial stack",100,65535)
        for value in deck:integer(value,"internal replay card",0,51)
        if len(set(deck))!=9:raise ProtocolError("internal replay cards overlap")
        integer(button,"button",0,1)
        self.lib.poker_reset(self.storage,0,button,(C.c_uint32*2)(*stacks),(C.c_uint8*9)(*deck))

    def step(self,action):
        # Only _rebuild invokes this after strict public legality validation.
        self.lib.poker_step(self.storage,0,integer(action,"action",-1,(1<<31)-1))

    def status(self):
        out=(C.c_int64*2)()
        if not self.lib.bb_game_status(self.storage,0,out):raise ProtocolError("native status unavailable")
        return tuple(out)

    def snapshot(self):
        # Trusted evaluator compatibility only. Policy construction uses the
        # separate public getter and acting player's own pair in native C.
        out=(C.c_uint32*29)();self.lib.poker_snapshot(self.storage,0,out)
        return list(out[:10]),[list(out[10+7*p:17+7*p]) for p in range(2)],list(out[-5:])

    def actions(self):
        out=(C.c_int32*8)();mask=self.lib.poker_actions(self.storage,0,out)
        return [out[i] for i in range(8) if mask&(1<<i)]

    def replace_replay(self,replay):
        if (type(replay) is not Native or replay.expected_sha256!=self.expected_sha256
                or len(replay.storage)!=len(self.storage)):
            raise ProtocolError("replay engine layout/source differs")
        # Policy RNG is a separate object and is deliberately untouched here.
        C.memmove(self.storage,replay.storage,len(self.storage))

class Context:
    def __init__(self,native,owner,seat,policy=7,samples=0,*,model_binding,expected_bridge_sha256):
        if native.players!=2:raise ProtocolError("HU Native required")
        expected_bridge_sha256=digest(expected_bridge_sha256,"bridge binary")
        if hashlib.sha256(Path(native.path).read_bytes()).hexdigest()!=expected_bridge_sha256:
            raise ProtocolError("native binary differs from source-bound bridge")
        self.expected_bridge_sha256=expected_bridge_sha256
        self.samples=integer(samples,"samples",0,128)
        if self.samples not in (0,32,64,128) or native.samples!=self.samples:
            raise ProtocolError("native/context samples mismatch")
        if type(owner) is not str or not owner or len(owner.encode())>512:raise ProtocolError("explicit owner namespace required")
        self.seat=integer(seat,"seat",0,1);self.policy=integer(policy,"policy",0,7)
        self.native=native;self.lib=native.lib;bind_library(self.lib)
        self.owner=(C.c_ubyte*32).from_buffer_copy(hashlib.sha256(
            b"clone-transport-owner-v1\0"+owner.encode()+b"\0"+bytes((self.seat,))).digest())
        required_binding={"model_npz_sha256","model_numeric_sha256","module_source_closure_sha256",
                          "module_binary_sha256","numeric_bytes","nodes"}
        if type(model_binding) is not dict or set(model_binding)!=required_binding:
            raise ProtocolError("exact explicit qualified model binding required")
        binding=ModelBinding();binding.model_domain=b"behavior-clone-full-model-v2"
        for name in ("model_npz_sha256","model_numeric_sha256","module_source_closure_sha256","module_binary_sha256"):
            setattr(binding,name,digest(model_binding.get(name),name).encode("ascii"))
        binding.numeric_bytes=integer(model_binding.get("numeric_bytes"),"model numeric bytes",1,524288)
        binding.nodes=integer(model_binding.get("nodes"),"model nodes",1,4064)
        self.ptr=self.lib.bb_create(self.owner,C.byref(binding),1<<self.seat)
        if not self.ptr:raise ProtocolError("v2 ABI, source, model identity or validation rejected activation")
        self.generation=0;self.started=False;self.closed=True

    def protocol_error(self,message):
        if self.ptr:self.lib.bb_protocol_error(self.ptr,str(message).encode()[:150])

    def _check(self,result):
        if not result:raise ProtocolError(self.lib.bb_last_error(self.ptr).decode("utf-8","replace"))

    def begin(self,generation):
        try:
            generation=integer(generation,"generation",1)
            self._check(self.lib.bb_begin(self.ptr,self.owner,generation))
            self._check(self.lib.bb_bind_native(self.ptr,self.owner,generation,self.native.storage,0))
        except Exception as error:
            self.protocol_error(error);raise
        self.generation=generation;self.started=False;self.closed=False

    def envelope(self,generation,allow_terminal=False):
        try:self._check(self.lib.bb_envelope(self.ptr,self.owner,integer(generation,"generation",1),int(allow_terminal)))
        except Exception as error:self.protocol_error(error);raise

    def start(self,public):
        self._check(self.lib.bb_start(self.ptr,self.owner,self.generation,C.byref(public)));self.started=True

    def observe(self,event):
        self._check(self.lib.bb_observe(self.ptr,self.owner,self.generation,
            C.byref(Public.from_buffer_copy(event.before)),event.action,C.byref(Public.from_buffer_copy(event.after))))

    def decide(self,request_id):
        try:
            action=C.c_int32()
            self._check(self.lib.bb_decide(self.ptr,self.owner,self.generation,self.native.storage,0,
                C.byref(self.native.rng),self.policy,self.native.samples,None,
                integer(request_id,"request ID",1),C.byref(action)))
            return action.value
        except Exception as error:self.protocol_error(error);raise

    def audit(self):
        out=Audit();self._check(self.lib.bb_audit(self.ptr,C.byref(out)))
        result={name:bytes(getattr(out,name)).hex() if isinstance(getattr(out,name),C.Array) else getattr(out,name)
                for name,_ in Audit._fields_}
        result.update({"protocol":"prospective_clone_bridge_v1","seat":self.seat,"policy":self.policy,
            "samples":self.samples,"closed":out.phase in (TERMINAL,ABANDONED,ERROR),"terminal":out.phase==TERMINAL})
        result["whole_hash"]=hashlib.sha256(json.dumps(result,sort_keys=True).encode()).hexdigest()
        return result

    def abandon(self):
        if not self.closed:
            if self.audit()["phase"]!=ERROR:self._check(self.lib.bb_abandon(self.ptr,self.owner,self.generation))
            self.closed=True

    def close(self):
        if self.ptr:self.lib.bb_free(self.ptr);self.ptr=None
        self.closed=True

class Session:
    """Sole externally validated response boundary; no HTTP or wager retry.

    The caller supplies only client_pos, hole_cards, board, action, terminal.
    Winnings and opponent revelations are not accepted into this object.
    A separate settlement adapter can receive them after closed=True.
    """
    def __init__(self,context):
        self.context=context;self.native=context.native
        self.generation=0;self.events=();self.board=();self.own=None;self.terminal=False;self.terminal_rng=None

    def ingest(self,generation,response):
        ctx=self.context
        try:
            # Generation capture precedes transport send. Check it before even
            # parsing the response shape; identical old bytes cannot revive it.
            ctx.envelope(generation,allow_terminal=self.terminal)
            if self.generation!=generation:
                if ctx.started:raise ProtocolError("new Session lost an active public prefix")
                self.generation=generation;self.events=();self.board=();self.own=None;self.terminal=False;self.terminal_rng=None
            audit=ctx.audit();current_rng=integer(ctx.native.rng.value,"native RNG receipt")
            if audit["pending"] and current_rng!=audit["pending_rng_after"]:
                raise ProtocolError("pending own-action RNG receipt changed before acknowledgement")
            if self.terminal and current_rng!=self.terminal_rng:
                raise ProtocolError("closed terminal RNG receipt changed")
            if type(response) is not dict or set(response)!={"client_pos","hole_cards","board","action","terminal"}:
                raise ProtocolError("strict sanitized response fields required")
            hero=integer(response["client_pos"],"client position",0,1)
            if hero!=ctx.seat:raise ProtocolError("response belongs to another seat")
            own=tuple(sorted(cards(response["hole_cards"],"own",(2,))))
            visible=cards(response["board"],"public board",(0,3,4,5))
            if set(own)&set(visible):raise ProtocolError("own/public card overlap")
            if self.own is not None and self.own!=own:raise ProtocolError("own pair changed within hand")
            if visible[:len(self.board)]!=self.board:raise ProtocolError("board prefix changed")
            history=response["action"]
            if type(history) is not str or len(history)>65535:raise ProtocolError("invalid public history text")
            marker=response["terminal"]
            if type(marker) is not bool:raise ProtocolError("strict terminal boolean required")
            replay,initial,state,events=_rebuild(ctx.native,hero,own,visible,history)
            if len(events)<len(self.events) or events[:len(self.events)]!=self.events:
                raise ProtocolError("complete ordered public prefix changed")
            terminal=state.actor==255
            if terminal!=marker or (not terminal and state.actor!=hero):raise ProtocolError("public terminal/acting seat mismatch")
            if self.terminal:
                if not terminal or events!=self.events:raise ProtocolError("closed terminal prefix changed")
                ctx._check(ctx.lib.bb_terminal_receipt(ctx.ptr,ctx.owner,generation,C.byref(state)))
                return {"terminal":True,"closed":True,"events":len(events),"new_events":0,"audit":ctx.audit()}
            if not ctx.started:ctx.start(initial)
            new_events=events[len(self.events):]
            for event in new_events:ctx.observe(event)
            # A terminal public event has already closed the native context.
            # The scratch core's dummy payout is never copied into a live
            # policy engine or exposed as evidence of revealed settlement.
            if not terminal:
                if (Path(replay.path).resolve()!=Path(ctx.native.path).resolve()
                        or len(replay.storage)!=len(ctx.native.storage)):
                    raise ProtocolError("replay storage/source mismatch")
                C.memmove(ctx.native.storage,replay.storage,len(ctx.native.storage))
            self.events=events;self.board=visible;self.own=own;self.terminal=terminal
            if terminal:self.terminal_rng=current_rng
            ctx.closed=terminal
            return {"terminal":terminal,"closed":ctx.closed,"events":len(events),"new_events":len(new_events),"audit":ctx.audit()}
        except Exception as error:ctx.protocol_error(error);raise

    def decide(self,request_id):
        if self.terminal:
            self.context.protocol_error("decision after terminal");raise ProtocolError("decision after terminal")
        return self.context.decide(request_id)

    def abandon(self):
        # An uncertain transport operation must end the generation. A caller
        # may record interruption but must never retry its wager or replace
        # the interrupted slot with a favorable/completed hand.
        self.context.abandon()
