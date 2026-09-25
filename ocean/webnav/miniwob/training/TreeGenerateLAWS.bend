import Base
import ./TreeGenerate.bend as G

# These laws cover the pure part of the bounded generator.  The row ABI
# offsets, packed byte order, and dirty-row clearing are checked by the native
# test because those properties live at the caller-owned Array boundary.

law generated_node_starts_initial:
  for spec: G.ShapeNode
  for +index: U32
  for +target: U32
  for +seed: U32
  {G.node_is_initial(G.make_node(spec, index, target, seed)) == True{} : Bool}

law generated_nodes_start_initial:
  for specs: +List<G.ShapeNode>
  for +index: U32
  for +target: U32
  for +seed: U32
  {G.all_initial(G.make_nodes(specs, index, target, seed)) == True{} : Bool}

law shape_sizes_are_bounded:
  for code: U32
  {U32.is_ge(G.shape_size(code), 1) && U32.is_le(G.shape_size(code), 8) == True{} : Bool}
