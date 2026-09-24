import Base
import ./Wire.bend as Wire

def Host.read() -> IO(Array<U32>):
  import "../../bend/read.c"

def Host.write(a: Array<U32>) -> IO(Unit):
  import "../../bend/write.c"

def main() -> IO(Unit):
  do IO<Unit>:
    a : Array<U32> <- Host.read()
    Host.write(Wire.batch(32n, a))

