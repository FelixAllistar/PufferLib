import Base
import ./Wire.bend as W

def Host.read() -> IO(Array<U32>):
  import "../../families/common/read.c"

def Host.write(a: Array<U32>) -> IO(Unit):
  import "../../families/common/write.c"

def main() -> IO(Unit):
  do IO<Unit>:
    a : Array<U32> <- Host.read()
    Host.write(W.row(0, a))
