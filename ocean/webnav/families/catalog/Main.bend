import Base
import ./Wire.bend as W

def Host.read() -> IO(Array<U32>):
  import "../common/read.c"

def Host.write(a: Array<U32>) -> IO(Unit):
  import "../common/write.c"

def main() -> IO(Unit):
  do IO<Unit>:
    a : Array<U32> <- Host.read()
    Host.write(W.batch(4n, a))
