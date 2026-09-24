import Base
import ./Wire.bend as Wire

# Standalone stock-Bend entry point.  The shared webnav build imports Wire.row
# directly; this entry point exists so the task lane can compile and exercise
# all 32 persistent rows without touching shared bridge files.

def Host.read() -> IO(Array<U32>):
  import "./read.c"

def Host.write(a: Array<U32>) -> IO(Unit):
  import "./write.c"

def main() -> IO(Unit):
  do IO<Unit>:
    a : Array<U32> <- Host.read()
    Host.write(Wire.batch(32n, a))
