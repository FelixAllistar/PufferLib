import Base
import ./Game.bend as Game

def Host.read() -> IO(Array<U32>):
  import "./read.c"

def Host.write(a: Array<U32>) -> IO(Unit):
  import "./write.c"

def main() -> IO(Unit):
  do IO<Unit>:
    a : Array<U32> <- Host.read()
    Host.write(Game.batch(32n, a))
