import Base
import ./TreeGenerate.bend as G
import ./TreeGenerateLAWS.bend as L

def bool_equal_self(+v: Bool) -> {G.bool_eq(v, v) == True{} : Bool}:
  match v:
    case False{}: {==}
    case True{}: {==}

def L.generated_node_starts_initial(spec, index, target, seed):
  match spec:
    case G.SN{kind, parent, subtree_end}: bool_equal_self(U32.is_eq(parent, 0))

def L.generated_nodes_start_initial(specs, index, target, seed):
  match specs:
    case Nil{}: {==}
    case Con{spec, rest}:
      Equal.trans(Bool,
        Bool.and(G.node_is_initial(G.make_node(spec, index, target, seed)), G.all_initial(G.make_nodes(rest, (index + 1 : U32), target, seed))),
        G.all_initial(G.make_nodes(rest, (index + 1 : U32), target, seed)), True{},
        Equal.cong(Bool, Bool,
          x => Bool.and(x, G.all_initial(G.make_nodes(rest, (index + 1 : U32), target, seed))),
          G.node_is_initial(G.make_node(spec, index, target, seed)), True{},
          L.generated_node_starts_initial(spec, index, target, seed)),
        L.generated_nodes_start_initial(rest, (index + 1 : U32), target, seed))

def size_is_bounded(+kind: G.ShapeKind) -> {U32.is_ge(G.size_for_kind(kind), 1) && U32.is_le(G.size_for_kind(kind), 8) == True{} : Bool}:
  match kind:
    case G.Single{}: {==}
    case G.Nested{}: {==}
    case G.Siblings{}: {==}
    case G.Mixed{}: {==}
    case G.Six{}: {==}
    case G.Eight{}: {==}

def L.shape_sizes_are_bounded(code):
  size_is_bounded(G.shape_kind(code))
