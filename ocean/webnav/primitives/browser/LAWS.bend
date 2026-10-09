import Base
import ./Model.bend as M

law idle_ready_preserves_state:
  for route: U32
  for count: U32
  for latency: U32
  for failures: U32
  for back: +List<U32>
  for forward: +List<U32>
  for elapsed: U32
  {M.step(M.S{route, count, latency, failures, back, forward, M.Ready{}}, M.Wait{}, elapsed) == M.S{route, count, latency, failures, back, forward, M.Ready{}} : M.State}

law invalid_navigation_preserves_state:
  for s: M.State
  for route: U32
  {M.navigate_if(False{}, s, route) == s : M.State}

law empty_back_preserves_state:
  for route: U32
  for count: U32
  for latency: U32
  for failures: U32
  for forward: +List<U32>
  for phase: M.Phase
  {M.go_back(M.S{route, count, latency, failures, Nil{}, forward, phase}) == M.S{route, count, latency, failures, Nil{}, forward, phase} : M.State}

law navigation_clears_forward:
  for forward: +List<U32>
  {M.navigate(M.S{0, 4, 50, 0, Nil{}, forward, M.Ready{}}, 2) == M.S{2, 4, 50, 0, [0], Nil{}, M.Loading{50}} : M.State}

law retry_preserves_history:
  for route: U32
  for count: U32
  for latency: U32
  for failures: U32
  for back: +List<U32>
  for forward: +List<U32>
  {M.retry(M.S{route, count, latency, failures, back, forward, M.Failed{}}) == M.S{route, count, latency, failures, back, forward, M.Loading{latency}} : M.State}

law back_then_forward_restores_route:
  {M.go_forward(M.go_back(M.S{2, 4, 50, 0, [1, 0], Nil{}, M.Ready{}})) == M.S{2, 4, 50, 0, [1, 0], Nil{}, M.Loading{50}} : M.State}

law reload_preserves_history:
  for route: U32
  for count: U32
  for latency: U32
  for failures: U32
  for back: +List<U32>
  for forward: +List<U32>
  for phase: M.Phase
  {M.act(M.Reload{}, M.S{route, count, latency, failures, back, forward, phase}) == M.S{route, count, latency, failures, back, forward, M.Loading{latency}} : M.State}

law same_route_navigation_preserves_history:
  for latency: U32
  for failures: U32
  for back: +List<U32>
  for forward: +List<U32>
  for phase: M.Phase
  {M.navigate(M.S{2, 4, latency, failures, back, forward, phase}, 2) == M.S{2, 4, latency, failures, back, forward, M.Loading{latency}} : M.State}

law wait_consumes_only_elapsed:
  {M.step(M.S{2, 4, 50, 0, [0], Nil{}, M.Loading{50}}, M.Wait{}, 20) == M.S{2, 4, 50, 0, [0], Nil{}, M.Loading{30}} : M.State}

law failed_load_is_recoverable:
  {M.step(M.step(M.step(M.S{2, 4, 50, 1, [0], Nil{}, M.Loading{50}}, M.Wait{}, 50), M.Retry{}, 0), M.Wait{}, 50) == M.S{2, 4, 50, 0, [0], Nil{}, M.Ready{}} : M.State}

law new_navigation_cancels_pending_load:
  {M.step(M.S{2, 4, 50, 1, [0], Nil{}, M.Loading{50}}, M.Navigate{3}, 20) == M.S{3, 4, 50, 1, [2, 0], Nil{}, M.Loading{50}} : M.State}

law history_has_fixed_capacity:
  {M.push(8, [7, 6, 5, 4, 3, 2, 1, 0]) == [8, 7, 6, 5, 4, 3, 2, 1] : +List<U32>}
