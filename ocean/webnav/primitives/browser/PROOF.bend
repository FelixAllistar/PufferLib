import Base
import ./Model.bend as M
import ./LAWS.bend as L

# Checked by the same guarded build entry used for family modules.
def L.idle_ready_preserves_state(route, count, latency, failures, back, forward, elapsed):
  {==}

def L.invalid_navigation_preserves_state(s, route):
  {==}

def L.empty_back_preserves_state(route, count, latency, failures, forward, phase):
  {==}

def L.navigation_clears_forward(forward):
  {==}

def L.retry_preserves_history(route, count, latency, failures, back, forward):
  {==}

def L.back_then_forward_restores_route():
  {==}

def L.reload_preserves_history(route, count, latency, failures, back, forward, phase):
  {==}

def L.same_route_navigation_preserves_history(latency, failures, back, forward, phase):
  {==}

def L.wait_consumes_only_elapsed():
  {==}

def L.failed_load_is_recoverable():
  {==}

def L.new_navigation_cancels_pending_load():
  {==}

def L.history_has_fixed_capacity():
  {==}
