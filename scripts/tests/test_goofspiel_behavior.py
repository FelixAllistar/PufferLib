import importlib.util
from pathlib import Path
import subprocess
import sys

SCRIPT = Path(__file__).resolve().parents[1] / "goofspiel_behavior.py"
spec = importlib.util.spec_from_file_location("goofspiel_behavior", SCRIPT)
behavior = importlib.util.module_from_spec(spec)
spec.loader.exec_module(behavior)


def test_groups_and_selection():
    scores = {"a": 0.1, "b": 0.2, "c": 0.3, "d": 0.4}
    distances = {}
    for a, b, value in [("a", "b", .03), ("b", "c", .03),
                        ("a", "c", .06), ("a", "d", .8),
                        ("b", "d", .7), ("c", "d", .6)]:
        distances[a, b] = distances[b, a] = value
    policies = list(scores)
    assert behavior.union_find(policies, distances, .05) == [["a", "b", "c"], ["d"]]
    assert behavior.select_diverse(policies, scores, distances, .05, 0, "best") == [
        "a", "c", "d"]
    assert behavior.select_diverse(policies, scores, distances, .05, 2, "best") == [
        "a", "c"]
    assert behavior.select_diverse(policies, scores, distances, .05, 2, "farthest") == [
        "a", "d"]


def test_report_cli(tmp_path):
    report = tmp_path / "behavior.tsv"
    report.write_text("policy\texact_exploitability\tstates\n"
                      "a.bin\t0.2\t10\nb.bin\t0.1\t10\n"
                      "policy_a\tpolicy_b\tjsd\tjs_distance\ttotal_variation\n"
                      "a.bin\tb.bin\t0.0001\t0.01\t0.02\n")
    scores, distances = behavior.read_behavior(report)
    assert scores == {"a.bin": .2, "b.bin": .1}
    assert distances == {("a.bin", "b.bin"): .01, ("b.bin", "a.bin"): .01}
    result = subprocess.run([sys.executable, str(SCRIPT), str(report)],
                            text=True, capture_output=True, check=True)
    assert "policies=2 groups=1 selected=1" in result.stdout
    assert (tmp_path / "behavior_selected.tsv").read_text() == (
        "policy\texact_exploitability\nb.bin\t0.100000000\n")
