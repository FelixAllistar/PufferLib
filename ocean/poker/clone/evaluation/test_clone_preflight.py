"""Pure, fabricated preflight checks; no native/model/log/network execution.

Run this file explicitly with a fresh --fixture-root beneath preflight_checks_v1.
Every external plan source, binary identity and qualification below is fabricated.
The only real workspace reads permitted are these tests and the four evaluator
source modules. Fabricated files are retained with the single-attempt receipt.
"""
from __future__ import annotations

import argparse
import builtins
from contextlib import ExitStack
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import sys
import unittest
from unittest import mock


STAGE = Path(__file__).resolve().parent
WORKSPACE = STAGE.parents[1]
ALLOWED_SOURCE = {str((STAGE / name).resolve()) for name in (
    "clone_eval.py", "clone_audit.py", "clone_adapter.py", "clone_series.py",
    "test_clone_preflight.py")}
FIXTURE_ROOT = None
OPENED_REAL_SOURCE = set()
OPENED_FABRICATED = set()


def audit_guard(event, arguments):
    if event in ("socket.__new__", "subprocess.Popen", "os.system", "os.fork", "ctypes.dlopen"):
        raise RuntimeError("preflight-only test forbids network, process and native-library calls")
    if event in ("os.remove", "os.rename", "os.rmdir"):
        raise RuntimeError("preflight fabricated evidence must remain preserved")
    if event == "os.mkdir":
        path = Path(arguments[0]).resolve()
        if not path.is_relative_to(FIXTURE_ROOT):
            raise RuntimeError("preflight directory write outside fabricated fixture root")
    if event != "open" or isinstance(arguments[0], int):
        return
    path = Path(arguments[0]).resolve()
    text = str(path)
    if text.startswith(str(FIXTURE_ROOT / "unused_bytecode_prefix") + os.sep) and text.endswith(".pyc"):
        raise FileNotFoundError("preflight source import bypasses bytecode caches")
    if path.suffix in (".npz", ".jsonl", ".so", ".dylib", ".dll"):
        raise RuntimeError("preflight forbids records, models and binary-library content")
    writing = any(letter in (arguments[1] or "") for letter in "wax+")
    if path.is_relative_to(FIXTURE_ROOT):
        if not writing:
            OPENED_FABRICATED.add(text)
        return
    if writing:
        raise RuntimeError("preflight file write outside fabricated fixture root")
    if text in ALLOWED_SOURCE:
        OPENED_REAL_SOURCE.add(text)
        return
    if path.is_relative_to(WORKSPACE):
        raise RuntimeError("preflight denied non-source workspace read")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n")


def model_binding():
    return {"model_version": 2, "size_domain": 2,
            "size_encoding": "legal_interior_fraction_f64_v2",
            "model_domain": "behavior-clone-full-model-v2",
            "model_npz_sha256": "1" * 64, "model_numeric_sha256": "2" * 64,
            "module_source_closure_sha256": "3" * 64, "module_binary_sha256": "4" * 64,
            "numeric_bytes": 227580, "nodes": 1924}


class FabricatedFixture:
    def __init__(self, folder):
        self.folder = folder
        folder.mkdir()
        self.poker = folder / "fabricated_poker_source"
        self.context = folder / "fabricated_context_source"
        self.poker.mkdir()
        self.context.mkdir()
        for path in (self.poker / "eval.py", self.context / "clone_transport.py",
                     self.context / "clone_public_replay.py"):
            path.write_text("raise RuntimeError('Fabricated identity must never be imported')\n")
        self.required = [STAGE / name for name in (
            "clone_eval.py", "clone_audit.py", "clone_adapter.py", "clone_series.py")]
        self.required.extend((self.poker / "eval.py", self.context / "clone_transport.py",
                              self.context / "clone_public_replay.py"))
        self.libraries = {}
        for arm in ("control", "candidate"):
            path = folder / ("fabricated_" + arm + "_identity.bin")
            path.write_bytes(("NOT AN EXECUTABLE OR MODEL: " + arm).encode("ascii"))
            self.libraries[arm] = {"path": str(path), "sha256": digest(path)}
        self.output = folder / "authorized_output_not_created"
        self.plan_path = folder / "fabricated_plan.json"
        self.authorization_path = folder / "fabricated_authorization.json"
        self.qualification_path = folder / "fabricated_qualification.json"
        self.binding = model_binding()
        self.qualification = {"approved_by": "/root", "schema": "clone-pre-play-qualification-v2",
            "model_binding_sha256": runner.model_binding_digest(self.binding),
            "candidate_library_sha256": self.libraries["candidate"]["sha256"],
            "fresh_validation_all26_passed": True, "fresh_assessment_all26_passed": True,
            "native_sampler_parity_and_cost_passed": True, "native_bridge_verification_passed": True,
            "actual_model_synthetic_pipeline_passed": True, "live_integration_passed": True}
        write_json(self.qualification_path, self.qualification)
        self.plan = {"schema": "clone-series-v2", "mode": "integration", "no_stakes": True,
            "resume_allowed": False, "replacement_hands_allowed": False,
            "policy_id": 7, "equity_samples": 0, "analysis": "actual_full_hand_raw_v2",
            "primary_metric": "raw", "model_binding": deepcopy(self.binding),
            "poker_source": str(self.poker), "context_source": str(self.context),
            "source_sha256": {str(path.resolve()): digest(path) for path in self.required},
            "libraries": deepcopy(self.libraries), "run_id": "fabricated_preflight_only",
            "contact": "fabricated-local-preflight-no-network",
            "jobs": [{"id": "00_candidate", "arm": "candidate", "hands": 2, "games": 1,
                      "seed": 202610186681, "run_id": "fabricated_preflight_only"},
                     {"id": "01_control", "arm": "control", "hands": 2, "games": 1,
                      "seed": 202610186681, "run_id": "fabricated_preflight_only"}]}
        self.rebind_qualification(self.plan, self.qualification)

    def rebind_qualification(self, plan, qualification):
        write_json(self.qualification_path, qualification)
        value = digest(self.qualification_path)
        plan["qualification"] = {"path": str(self.qualification_path), "sha256": value}
        plan["source_sha256"][str(self.qualification_path)] = value

    def invocation(self, plan=None, authorization_changes=None):
        plan = deepcopy(self.plan if plan is None else plan)
        write_json(self.plan_path, plan)
        plan_hash = digest(self.plan_path)
        authorization = {"plan_sha256": plan_hash, "authorized": True, "mode": plan["mode"],
                         "output_directory": str(self.output.resolve())}
        if authorization_changes:
            authorization.update(authorization_changes)
        write_json(self.authorization_path, authorization)
        execution = {"plan_path": str(self.plan_path), "plan_sha256": plan_hash,
                     "authorization_path": str(self.authorization_path),
                     "authorization_sha256": digest(self.authorization_path)}
        job = plan["jobs"][0]
        args = series.arguments(plan, job, plan_hash, self.output / (job["id"] + ".jsonl"))
        return args, execution


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.fixture = FabricatedFixture(FIXTURE_ROOT / self._testMethodName)
        self.guards = ExitStack()
        self.addCleanup(self.guards.close)
        self.native_loader = self.guards.enter_context(mock.patch.object(
            runner, "load_module", side_effect=AssertionError("preflight reached native/evaluator import")))
        self.transport_loader = self.guards.enter_context(mock.patch.object(
            runner, "load_transport", side_effect=AssertionError("preflight reached transport import")))
        self.journal = self.guards.enter_context(mock.patch.object(
            runner, "Journal", side_effect=AssertionError("preflight started a hand journal")))
        self.executor = self.guards.enter_context(mock.patch.object(
            runner, "ThreadPoolExecutor", side_effect=AssertionError("preflight started playing workers")))

    def tearDown(self):
        self.native_loader.assert_not_called()
        self.transport_loader.assert_not_called()
        self.journal.assert_not_called()
        self.executor.assert_not_called()
        self.assertFalse(self.fixture.output.exists())

    def test_direct_run_without_live_execution_stops_before_import_or_http(self):
        args, _ = self.fixture.invocation()
        original_import = builtins.__import__
        imports = []

        def observed_import(name, *arguments, **keywords):
            imports.append(name)
            if name.split(".")[0] in ("requests", "ctypes", "numpy", "eval7"):
                raise AssertionError("ungated live run reached a native or HTTP import")
            return original_import(name, *arguments, **keywords)

        with mock.patch("builtins.__import__", side_effect=observed_import):
            with self.assertRaisesRegex(runner.JobError, "authorized clone series"):
                runner.run(args)
        self.assertEqual(imports, [])

    def test_load_plan_requires_each_source_before_binary_identity_reads(self):
        args, execution = self.fixture.invocation()
        self.assertEqual(series.load_plan(execution["plan_path"], execution["plan_sha256"]), self.fixture.plan)
        real_digest = runner.sha256
        for removed in self.fixture.required:
            with self.subTest(source=removed.name, directory=removed.parent.name):
                plan = deepcopy(self.fixture.plan)
                del plan["source_sha256"][str(removed.resolve())]
                _, execution = self.fixture.invocation(plan)
                with mock.patch.object(runner, "sha256", wraps=real_digest) as hashed:
                    with self.assertRaisesRegex(audit.AuditError, "source is absent"):
                        series.load_plan(execution["plan_path"], execution["plan_sha256"])
                opened = {str(Path(call.args[0]).resolve()) for call in hashed.call_args_list}
                self.assertTrue(opened.isdisjoint({item["path"] for item in self.fixture.libraries.values()}))

    def test_load_plan_rejects_wrong_imported_module_even_when_its_file_is_bound(self):
        for name, module in (("clone_eval.py", runner), ("clone_audit.py", audit),
                             ("clone_adapter.py", adapter)):
            with self.subTest(module=name):
                foreign = self.fixture.folder / ("foreign_" + name)
                foreign.write_text("# Fabricated module identity; never imported.\n")
                plan = deepcopy(self.fixture.plan)
                plan["source_sha256"][str(foreign)] = digest(foreign)
                _, execution = self.fixture.invocation(plan)
                with mock.patch.object(module, "__file__", str(foreign)):
                    with self.assertRaisesRegex(audit.AuditError, "imported evaluator source differs"):
                        series.load_plan(execution["plan_path"], execution["plan_sha256"])

    def test_exact_fabricated_plan_authorization_and_job_accept_without_execution(self):
        args, execution = self.fixture.invocation()
        runner.verify_live_execution(args, execution)

    def test_every_changed_job_argument_including_output_rejects(self):
        args, execution = self.fixture.invocation()
        changes = {"style": 6, "samples": 32, "arm": "control", "hands": 4, "games": 2,
            "seed": args.seed + 1, "library": Path(self.fixture.libraries["control"]["path"]),
            "expected_library_sha256": "5" * 64, "control_library": self.fixture.folder / "other_control.bin",
            "expected_control_sha256": "6" * 64, "context_library": self.fixture.folder / "other_context.bin",
            "expected_context_sha256": "7" * 64, "poker_source": self.fixture.folder / "other_poker",
            "context_source": self.fixture.folder / "other_transport", "output": self.fixture.folder / "different_output.jsonl",
            "job": "unplanned_job", "run_id": "different_run", "manifest_sha256": "8" * 64,
            "contact": "different_contact", "model_binding": {**args.model_binding, "nodes": 1925}}
        self.assertEqual(set(changes), set(vars(args)))
        for field, changed in changes.items():
            with self.subTest(argument=field):
                modified = deepcopy(args)
                setattr(modified, field, changed)
                with self.assertRaises(runner.JobError):
                    runner.verify_live_execution(modified, execution)

    def test_unopened_qualification_and_each_incomplete_prerequisite_rejects(self):
        plan = deepcopy(self.fixture.plan)
        del plan["qualification"]
        args, execution = self.fixture.invocation(plan)
        with self.assertRaisesRegex(audit.AuditError, "qualification receipt required"):
            runner.verify_live_execution(args, execution)
        for field in ("fresh_validation_all26_passed", "fresh_assessment_all26_passed",
                      "native_sampler_parity_and_cost_passed", "native_bridge_verification_passed",
                      "actual_model_synthetic_pipeline_passed"):
            with self.subTest(prerequisite=field):
                plan = deepcopy(self.fixture.plan)
                qualification = {**self.fixture.qualification, field: False}
                self.fixture.rebind_qualification(plan, qualification)
                args, execution = self.fixture.invocation(plan)
                with self.assertRaisesRegex(audit.AuditError, "unfinished pre-play qualification: " + field):
                    runner.verify_live_execution(args, execution)
        plan = deepcopy(self.fixture.plan)
        self.fixture.rebind_qualification(plan, self.fixture.qualification)
        missing = self.fixture.folder / "never_opened_qualification.json"
        del plan["source_sha256"][str(self.fixture.qualification_path)]
        plan["qualification"] = {"path": str(missing), "sha256": "9" * 64}
        plan["source_sha256"][str(missing)] = "9" * 64
        args, execution = self.fixture.invocation(plan)
        with self.assertRaises(FileNotFoundError):
            runner.verify_live_execution(args, execution)

    def test_unopened_or_changed_authorization_and_unbound_output_reject(self):
        args, execution = self.fixture.invocation()
        absent = {**execution, "authorization_path": str(self.fixture.folder / "never_opened_authorization.json")}
        with self.assertRaises(FileNotFoundError):
            runner.verify_live_execution(args, absent)
        with self.assertRaisesRegex(runner.JobError, "authorization changed"):
            runner.verify_live_execution(args, {**execution, "authorization_sha256": "a" * 64})
        for change in ({"authorized": False}, {"mode": "confirmatory"}, {"plan_sha256": "b" * 64},
                       {"output_directory": "relative_output"}, {"output_directory": None},
                       {"output_directory": str(self.fixture.folder / "unused" / ".." / "noncanonical")}):
            with self.subTest(authorization=change):
                args, execution = self.fixture.invocation(authorization_changes=change)
                with self.assertRaises(audit.AuditError):
                    runner.verify_live_execution(args, execution)

    def test_unsafe_job_names_cannot_escape_the_authorized_slot(self):
        for identity in ("../escape", "nested/job", ".", "job.with.dot", ""):
            with self.subTest(job=identity):
                plan = deepcopy(self.fixture.plan)
                plan["jobs"][0]["id"] = identity
                args, execution = self.fixture.invocation(plan)
                with self.assertRaisesRegex(audit.AuditError, "safe fixed slot name"):
                    runner.verify_live_execution(args, execution)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-root", type=Path, required=True)
    arguments = parser.parse_args()
    FIXTURE_ROOT = arguments.fixture_root.resolve()
    if (not FIXTURE_ROOT.is_relative_to(STAGE / "preflight_checks_v1")
            or not FIXTURE_ROOT.is_dir() or any(FIXTURE_ROOT.iterdir())):
        raise SystemExit("A new empty fabricated fixture root inside preflight_checks_v1 is required")
    sys.dont_write_bytecode = True
    sys.pycache_prefix = str(FIXTURE_ROOT / "unused_bytecode_prefix")
    sys.addaudithook(audit_guard)
    import clone_eval as runner
    import clone_audit as audit
    import clone_adapter as adapter
    import clone_series as series

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PreflightTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print(json.dumps({"real_source_reads": sorted(OPENED_REAL_SOURCE),
        "fabricated_files_read": len(OPENED_FABRICATED), "real_model_archives_loaded": 0,
        "real_binary_libraries_loaded": 0, "real_logs_read": 0, "network_calls": 0,
        "native_policy_calls": 0, "native_pipeline_runs": 0, "older_contract_tests_run": 0,
        "tests_run": result.testsRun, "passed": result.wasSuccessful()}, sort_keys=True))
    raise SystemExit(0 if result.wasSuccessful() else 1)
