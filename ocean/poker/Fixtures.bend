import Base
import ./Model.bend as M

def main() -> IO(Unit):
  do IO<Unit>:
    IO.print(U32.show(M.pot([50, 150, 200, 100], 0, 50)))
    IO.print(U32.show(M.pot([50, 150, 200, 100], 50, 150)))
    IO.print(U32.show(M.pot([50, 150, 200, 100], 150, 200)))
    IO.print(U32.show(M.share(101, 2, 0)))
    IO.print(U32.show(M.share(101, 2, 1)))
