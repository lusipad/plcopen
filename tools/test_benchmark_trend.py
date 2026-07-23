import importlib.util
import json
import shutil
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
TOOL_PATH = ROOT / "tools" / "benchmark_trend.py"
POLICY_PATH = ROOT / "ci" / "benchmark-trend-policy.json"


def load_tool_module():
    spec = importlib.util.spec_from_file_location("benchmark_trend", TOOL_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def benchmark_stdout(
    *,
    cartesian=100.0,
    serial_chain=20.0,
    scalar_scan=80.0,
    scalar_unit=10.0,
    mixed_instructions=11,
    mixed_instruction=12.0,
    native_scan=60.0,
    window=40.0,
    platform="windows",
    compiler="msvc",
    compiler_version="19.50.35726",
    build="release",
):
    return "\n".join(
        [
            f"CARTESIAN_METRICS cartesian_ik_cycle_us={cartesian:.3f} budget_us=50",
            (
                f"SERIAL_CHAIN_METRICS serial_chain_ik_us={serial_chain:.3f} "
                "serial_chain_budget_us=30"
            ),
            (
                "ST_WCET_METRICS "
                f"platform={platform} compiler={compiler} compiler_version={compiler_version} "
                f"build={build} calibration_eligible=1 certified_wcet=0 "
                f"scalar_work_units=7 scalar_observed_ns_per_scan={scalar_scan:.3f} "
                f"scalar_observed_ns_per_work_unit={scalar_unit:.3f} "
                f"mixed_instructions={mixed_instructions} "
                f"mixed_observed_ns_per_instruction={mixed_instruction:.3f} "
                f"native_work_units=6 "
                f"native_fb_instances=1 native_observed_ns_per_scan={native_scan:.3f}"
            ),
            f"WINDOW_METRICS segments=64 window_replan_us={window:.3f}",
            "",
        ]
    )


def oracle_stdout(domains=None):
    rows = domains or [
        {
            "domain": "linear",
            "attempted": 12,
            "planner_fail": 0,
            "oracle_miss": 0,
            "negative_fail": 0,
            "compared": 12,
            "excess_min": 0,
            "excess_max": 1,
            "excess_sum": 3,
            "hard_gate": 0,
        },
        {
            "domain": "circular",
            "attempted": 8,
            "planner_fail": 0,
            "oracle_miss": 0,
            "negative_fail": 0,
            "compared": 8,
            "excess_min": 0,
            "excess_max": 2,
            "excess_sum": 4,
            "hard_gate": 0,
        },
    ]
    lines = []
    for row in rows:
        lines.append(
            "OTG_ORACLE_METRICS "
            f"domain={row['domain']} attempted={row['attempted']} "
            f"planner_fail={row['planner_fail']} oracle_miss={row['oracle_miss']} "
            f"negative_fail={row['negative_fail']} compared={row['compared']} "
            f"excess_min={row['excess_min']} excess_max={row['excess_max']} "
            f"excess_sum={row['excess_sum']} hard_gate={row['hard_gate']}"
        )
    lines.append("")
    return "\n".join(lines)


class BenchmarkTrendToolTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = Path(tempfile.mkdtemp(prefix="benchmark-trend-tests-"))
        self.output_path = self.temp_dir / "result.json"

    def tearDown(self):
        shutil.rmtree(self.temp_dir)

    def write_emitter(self, name, outputs):
        script_path = self.temp_dir / f"{name}.py"
        counter_path = self.temp_dir / f"{name}.count"
        body = textwrap.dedent(
            f"""
            import pathlib
            import sys

            counter_path = pathlib.Path(r\"{counter_path}\")
            count = int(counter_path.read_text(encoding=\"utf-8\") if counter_path.exists() else \"0\")
            counter_path.write_text(str(count + 1), encoding=\"utf-8\")
            outputs = {json.dumps(outputs)}
            index = count if count < len(outputs) else len(outputs) - 1
            sys.stdout.write(outputs[index])
            """
        ).strip()
        script_path.write_text(body, encoding="utf-8")
        return script_path

    def run_main(self, args):
        module = load_tool_module()
        return module.main(args)

    def test_record_mode_emits_versioned_json(self):
        benchmark = self.write_emitter("record-benchmark", [benchmark_stdout()])
        oracle = self.write_emitter("record-oracle", [oracle_stdout()])

        exit_code = self.run_main(
            [
                "record",
                "--policy",
                str(POLICY_PATH),
                "--benchmark",
                str(benchmark),
                "--oracle",
                str(oracle),
                "--output",
                str(self.output_path),
                "--sha",
                "abc123",
            ]
        )

        self.assertEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["mode"], "record")
        self.assertEqual(payload["sha"], "abc123")
        self.assertEqual(payload["verdict"]["status"], "pass")
        self.assertIn("benchmark", payload)
        self.assertIn("oracle", payload)
        self.assertIn("sample", payload["benchmark"])
        self.assertIn("sample", payload["oracle"])

    def test_record_accepts_non_hard_negative_excess(self):
        benchmark = self.write_emitter("negative-oracle-benchmark", [benchmark_stdout()])
        oracle = self.write_emitter(
            "negative-oracle",
            [
                oracle_stdout(
                    [
                        {
                            "domain": "high-a0",
                            "attempted": 2,
                            "planner_fail": 0,
                            "oracle_miss": 0,
                            "negative_fail": 0,
                            "compared": 2,
                            "excess_min": -3,
                            "excess_max": 1,
                            "excess_sum": -2,
                            "hard_gate": 0,
                        }
                    ]
                )
            ],
        )

        exit_code = self.run_main(
            [
                "record",
                "--policy",
                str(POLICY_PATH),
                "--benchmark",
                str(benchmark),
                "--oracle",
                str(oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertEqual(exit_code, 0)

    def test_compare_warns_on_ten_percent_regression(self):
        base_benchmark = self.write_emitter(
            "warn-base-benchmark",
            [
                benchmark_stdout(
                    cartesian=100.0,
                    serial_chain=20.0,
                    mixed_instruction=10.0,
                    window=40.0,
                )
            ]
            * 10,
        )
        head_benchmark = self.write_emitter(
            "warn-head-benchmark",
            [
                benchmark_stdout(
                    cartesian=111.0,
                    serial_chain=22.2,
                    mixed_instruction=11.1,
                    window=44.4,
                )
            ]
            * 10,
        )
        base_oracle = self.write_emitter("warn-base-oracle", [oracle_stdout()] * 2)
        head_oracle = self.write_emitter("warn-head-oracle", [oracle_stdout()] * 2)

        exit_code = self.run_main(
            [
                "compare",
                "--policy",
                str(POLICY_PATH),
                "--base-benchmark",
                str(base_benchmark),
                "--head-benchmark",
                str(head_benchmark),
                "--base-oracle",
                str(base_oracle),
                "--head-oracle",
                str(head_oracle),
                "--output",
                str(self.output_path),
                "--base-sha",
                "base123",
                "--head-sha",
                "head123",
            ]
        )

        self.assertEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertEqual(payload["verdict"]["status"], "warn")
        self.assertEqual(len(payload["benchmark"]["initial_pairs"]), 9)
        self.assertFalse(payload["benchmark"]["confirmation_pairs"])

    def test_compare_fails_only_after_confirmation(self):
        base_benchmark = self.write_emitter(
            "fail-base-benchmark",
            [
                benchmark_stdout(
                    cartesian=100.0,
                    serial_chain=20.0,
                    mixed_instruction=10.0,
                    window=40.0,
                )
            ]
            * 19,
        )
        head_benchmark = self.write_emitter(
            "fail-head-benchmark",
            [
                benchmark_stdout(
                    cartesian=130.0,
                    serial_chain=26.0,
                    mixed_instruction=13.0,
                    window=52.0,
                )
            ]
            * 19,
        )
        base_oracle = self.write_emitter("fail-base-oracle", [oracle_stdout()] * 2)
        head_oracle = self.write_emitter("fail-head-oracle", [oracle_stdout()] * 2)

        exit_code = self.run_main(
            [
                "compare",
                "--policy",
                str(POLICY_PATH),
                "--base-benchmark",
                str(base_benchmark),
                "--head-benchmark",
                str(head_benchmark),
                "--base-oracle",
                str(base_oracle),
                "--head-oracle",
                str(head_oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertEqual(payload["verdict"]["status"], "fail")
        self.assertEqual(len(payload["benchmark"]["initial_pairs"]), 9)
        self.assertEqual(len(payload["benchmark"]["confirmation_pairs"]), 9)

    def test_compare_fails_on_otg_regression(self):
        base_benchmark = self.write_emitter("otg-base-benchmark", [benchmark_stdout()] * 10)
        head_benchmark = self.write_emitter("otg-head-benchmark", [benchmark_stdout()] * 10)
        base_oracle = self.write_emitter("otg-base-oracle", [oracle_stdout()] * 2)
        head_oracle = self.write_emitter(
            "otg-head-oracle",
            [
                oracle_stdout(
                    [
                        {
                            "domain": "linear",
                            "attempted": 13,
                            "planner_fail": 0,
                            "oracle_miss": 1,
                            "negative_fail": 0,
                            "compared": 12,
                            "excess_min": 0,
                            "excess_max": 1,
                            "excess_sum": 3,
                            "hard_gate": 0,
                        }
                    ]
                )
            ]
            * 2,
        )

        exit_code = self.run_main(
            [
                "compare",
                "--policy",
                str(POLICY_PATH),
                "--base-benchmark",
                str(base_benchmark),
                "--head-benchmark",
                str(head_benchmark),
                "--base-oracle",
                str(base_oracle),
                "--head-oracle",
                str(head_oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertEqual(payload["verdict"]["status"], "fail")
        self.assertTrue(
            any("domain set" in item or "oracle_miss" in item for item in payload["verdict"]["errors"])
        )

    def test_compare_fails_on_planner_and_negative_fail_regression(self):
        base_benchmark = self.write_emitter("otg2-base-benchmark", [benchmark_stdout()] * 10)
        head_benchmark = self.write_emitter("otg2-head-benchmark", [benchmark_stdout()] * 10)
        base_oracle = self.write_emitter("otg2-base-oracle", [oracle_stdout()] * 2)
        head_oracle = self.write_emitter(
            "otg2-head-oracle",
            [
                oracle_stdout(
                    [
                        {
                            "domain": "linear",
                            "attempted": 14,
                            "planner_fail": 1,
                            "oracle_miss": 0,
                            "negative_fail": 1,
                            "compared": 12,
                            "excess_min": 0,
                            "excess_max": 1,
                            "excess_sum": 3,
                            "hard_gate": 0,
                        },
                        {
                            "domain": "circular",
                            "attempted": 8,
                            "planner_fail": 0,
                            "oracle_miss": 0,
                            "negative_fail": 0,
                            "compared": 8,
                            "excess_min": 0,
                            "excess_max": 2,
                            "excess_sum": 4,
                            "hard_gate": 0,
                        },
                    ]
                )
            ]
            * 2,
        )

        exit_code = self.run_main(
            [
                "compare",
                "--policy",
                str(POLICY_PATH),
                "--base-benchmark",
                str(base_benchmark),
                "--head-benchmark",
                str(head_benchmark),
                "--base-oracle",
                str(base_oracle),
                "--head-oracle",
                str(head_oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertEqual(payload["verdict"]["status"], "fail")
        self.assertTrue(any("planner_fail" in item for item in payload["verdict"]["errors"]))
        self.assertTrue(any("negative_fail" in item for item in payload["verdict"]["errors"]))

    def test_record_rejects_missing_and_duplicate_fields(self):
        malformed = "\n".join(
            [
                "CARTESIAN_METRICS cartesian_ik_cycle_us=100.0 budget_us=50 budget_us=50",
                "ST_WCET_METRICS platform=windows compiler=msvc compiler_version=19.50.35726 build=release calibration_eligible=1 certified_wcet=0 scalar_work_units=7 scalar_observed_ns_per_scan=80 scalar_observed_ns_per_work_unit=10 mixed_instructions=11 mixed_observed_ns_per_instruction=12 native_work_units=6 native_fb_instances=1 native_observed_ns_per_scan=60",
                "",
            ]
        )
        benchmark = self.write_emitter("malformed-benchmark", [malformed])
        oracle = self.write_emitter("malformed-oracle", [oracle_stdout()])

        exit_code = self.run_main(
            [
                "record",
                "--policy",
                str(POLICY_PATH),
                "--benchmark",
                str(benchmark),
                "--oracle",
                str(oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)

    def test_record_rejects_shell_injection_nan_and_overlong_lines(self):
        cases = [
            benchmark_stdout(compiler="msvc;rm"),
            "\n".join(
                [
                    "CARTESIAN_METRICS cartesian_ik_cycle_us=NaN budget_us=50",
                    "SERIAL_CHAIN_METRICS serial_chain_ik_us=20.0 serial_chain_budget_us=30",
                    "ST_WCET_METRICS platform=windows compiler=msvc compiler_version=19.50.35726 build=release calibration_eligible=1 certified_wcet=0 scalar_work_units=7 scalar_observed_ns_per_scan=80 scalar_observed_ns_per_work_unit=10 mixed_instructions=11 mixed_observed_ns_per_instruction=12 native_work_units=6 native_fb_instances=1 native_observed_ns_per_scan=60",
                    "WINDOW_METRICS segments=64 window_replan_us=40",
                    "",
                ]
            ),
            "CARTESIAN_METRICS " + ("x" * 600),
        ]

        for index, stdout in enumerate(cases):
            with self.subTest(index=index):
                benchmark = self.write_emitter(f"unsafe-benchmark-{index}", [stdout])
                oracle = self.write_emitter(f"unsafe-oracle-{index}", [oracle_stdout()])
                exit_code = self.run_main(
                    [
                        "record",
                        "--policy",
                        str(POLICY_PATH),
                        "--benchmark",
                        str(benchmark),
                        "--oracle",
                        str(oracle),
                        "--output",
                        str(self.output_path),
                    ]
                )
                self.assertNotEqual(exit_code, 0)

    def test_record_rejects_missing_or_injected_mixed_fields(self):
        cases = [
            "\n".join(
                [
                    "CARTESIAN_METRICS cartesian_ik_cycle_us=100.0 budget_us=50",
                    "SERIAL_CHAIN_METRICS serial_chain_ik_us=20.0 serial_chain_budget_us=30",
                    "ST_WCET_METRICS platform=windows compiler=msvc compiler_version=19.50.35726 build=release calibration_eligible=1 certified_wcet=0 scalar_work_units=7 scalar_observed_ns_per_scan=80 scalar_observed_ns_per_work_unit=10 native_work_units=6 native_fb_instances=1 native_observed_ns_per_scan=60",
                    "WINDOW_METRICS segments=64 window_replan_us=40",
                    "",
                ]
            ),
            benchmark_stdout().replace(
                "mixed_instructions=11",
                "mixed_instructions=11;calc",
            ),
        ]

        for index, stdout in enumerate(cases):
            with self.subTest(index=index):
                benchmark = self.write_emitter(f"missing-mixed-benchmark-{index}", [stdout])
                oracle = self.write_emitter(f"missing-mixed-oracle-{index}", [oracle_stdout()])
                exit_code = self.run_main(
                    [
                        "record",
                        "--policy",
                        str(POLICY_PATH),
                        "--benchmark",
                        str(benchmark),
                        "--oracle",
                        str(oracle),
                        "--output",
                        str(self.output_path),
                    ]
                )
                self.assertNotEqual(exit_code, 0)

    def test_compare_rejects_environment_mismatch(self):
        base_benchmark = self.write_emitter("env-base-benchmark", [benchmark_stdout()] * 10)
        head_benchmark = self.write_emitter(
            "env-head-benchmark",
            [benchmark_stdout(compiler="clang")] * 10,
        )
        base_oracle = self.write_emitter("env-base-oracle", [oracle_stdout()] * 2)
        head_oracle = self.write_emitter("env-head-oracle", [oracle_stdout()] * 2)

        exit_code = self.run_main(
            [
                "compare",
                "--policy",
                str(POLICY_PATH),
                "--base-benchmark",
                str(base_benchmark),
                "--head-benchmark",
                str(head_benchmark),
                "--base-oracle",
                str(base_oracle),
                "--head-oracle",
                str(head_oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)

        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertEqual(payload["verdict"]["status"], "fail")
        self.assertTrue(any("environment" in item for item in payload["verdict"]["errors"]))

    def test_compare_rejects_workload_identity_mismatch(self):
        base_benchmark = self.write_emitter(
            "identity-base-benchmark", [benchmark_stdout(mixed_instructions=11)] * 10
        )
        head_benchmark = self.write_emitter(
            "identity-head-benchmark", [benchmark_stdout(mixed_instructions=12)] * 10
        )
        base_oracle = self.write_emitter("identity-base-oracle", [oracle_stdout()] * 2)
        head_oracle = self.write_emitter("identity-head-oracle", [oracle_stdout()] * 2)

        exit_code = self.run_main(
            [
                "compare",
                "--policy",
                str(POLICY_PATH),
                "--base-benchmark",
                str(base_benchmark),
                "--head-benchmark",
                str(head_benchmark),
                "--base-oracle",
                str(base_oracle),
                "--head-oracle",
                str(head_oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertEqual(payload["verdict"]["status"], "fail")
        self.assertTrue(any("workload identity" in item for item in payload["verdict"]["errors"]))

    def test_record_rejects_negative_timing_metric(self):
        benchmark = self.write_emitter(
            "negative-benchmark",
            [benchmark_stdout(window=-1.0)],
        )
        oracle = self.write_emitter("negative-oracle", [oracle_stdout()])

        exit_code = self.run_main(
            [
                "record",
                "--policy",
                str(POLICY_PATH),
                "--benchmark",
                str(benchmark),
                "--oracle",
                str(oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertTrue(any("window_replan_us must be > 0" in item for item in payload["verdict"]["errors"]))

    def test_record_rejects_otg_accounting_mismatch(self):
        benchmark = self.write_emitter("otg-account-benchmark", [benchmark_stdout()])
        oracle = self.write_emitter(
            "otg-account-oracle",
            [
                oracle_stdout(
                    [
                        {
                            "domain": "linear",
                            "attempted": 12,
                            "planner_fail": 1,
                            "oracle_miss": 0,
                            "negative_fail": 0,
                            "compared": 12,
                            "excess_min": 0,
                            "excess_max": 1,
                            "excess_sum": 3,
                            "hard_gate": 0,
                        }
                    ]
                )
            ],
        )

        exit_code = self.run_main(
            [
                "record",
                "--policy",
                str(POLICY_PATH),
                "--benchmark",
                str(benchmark),
                "--oracle",
                str(oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertTrue(any("attempted accounting mismatch" in item for item in payload["verdict"]["errors"]))

    def test_policy_rejects_unknown_fields(self):
        benchmark = self.write_emitter("policy-benchmark", [benchmark_stdout()])
        oracle = self.write_emitter("policy-oracle", [oracle_stdout()])
        policy_path = self.temp_dir / "policy.json"
        policy = json.loads(POLICY_PATH.read_text(encoding="utf-8"))
        policy["unexpected"] = True
        policy_path.write_text(json.dumps(policy), encoding="utf-8")

        exit_code = self.run_main(
            [
                "record",
                "--policy",
                str(policy_path),
                "--benchmark",
                str(benchmark),
                "--oracle",
                str(oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)

    def test_policy_rejects_invalid_types(self):
        benchmark = self.write_emitter("policy-type-benchmark", [benchmark_stdout()])
        oracle = self.write_emitter("policy-type-oracle", [oracle_stdout()])
        policy_path = self.temp_dir / "bad-policy.json"
        policy = json.loads(POLICY_PATH.read_text(encoding="utf-8"))
        policy["sample_pairs"] = "9"
        policy["thresholds"]["warn_ratio"] = "1.1"
        policy_path.write_text(json.dumps(policy), encoding="utf-8")

        exit_code = self.run_main(
            [
                "record",
                "--policy",
                str(policy_path),
                "--benchmark",
                str(benchmark),
                "--oracle",
                str(oracle),
                "--output",
                str(self.output_path),
            ]
        )

        self.assertNotEqual(exit_code, 0)
        payload = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertTrue(any("sample_pairs" in item or "warn_ratio" in item for item in payload["verdict"]["errors"]))


if __name__ == "__main__":
    unittest.main()
