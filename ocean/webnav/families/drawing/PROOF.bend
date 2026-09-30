import Base
import ./LAWS.bend as L

def L.released_move_is_inert(points, point): {==}
def L.new_stroke_replaces_previous_samples(points, point): {==}
def L.timeout_preempts_pointer_down(points, point): {==}
def L.terminal_absorbs_pointer(points, active, action, elapsed): {==}

def L.submit_deadline_preempts_scoring(points, active, task, center, horizontal): {==}
def L.finished_reward_absorbs_all_actions(points, active, reward, action, elapsed, task, center, horizontal): {==}
