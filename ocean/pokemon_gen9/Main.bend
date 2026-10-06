import Base
import ./Wire.bend as W
import ./Mem.bend as S
import ./EffectStore.bend as Effects

type EffectImage is Data:
  EmptyEffectImage{}
  SavedEffectImage{heap: Effects.Heap}

def Host.read() -> IO(Array<U32>):
  import "./read.c"

def Host.write(a: Array<U32>) -> IO(Unit):
  import "./write.c"

def Host.read_effects() -> IO(EffectImage):
  import "./read_effects.c"

def Host.write_effects(image: EffectImage) -> IO(Unit):
  import "./write_effects.c"

def restore_effects(image: EffectImage) -> Effects.Heap:
  match image:
    case EmptyEffectImage{}: Effects.empty(0)
    case SavedEffectImage{heap}: heap

def flush(world: S.World) -> IO(Unit):
  S.MemoryWorld{words,effects} = world
  do IO<Unit>:
    Host.write_effects(SavedEffectImage{effects})
    Host.write(words)

def main() -> IO(Unit):
  do IO<Unit>:
    a : Array<U32> <- Host.read()
    image : EffectImage <- Host.read_effects()
    flush(W.execute(S.MemoryWorld{a,restore_effects(image)}))
