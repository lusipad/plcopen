import argparse
import json
import math
import os
import platform
import re
import statistics
import subprocess
import sys
from pathlib import Path


SCHEMA_VERSION = 1
SAFE_TOKEN = re.compile(r"^[A-Za-z0-9_.-]+$")

BENCHMARK_SPECS = {
    "CARTESIAN_METRICS": {
        "cartesian_ik_cycle_us": "float",
        "budget_us": "float",
    },
    "SERIAL_CHAIN_METRICS": {
        "serial_chain_ik_us": "float",
        "serial_chain_budget_us": "float",
    },
    "ST_WCET_METRICS": {
        "platform": "safe",
        "compiler": "safe",
        "compiler_version": "version",
        "build": "safe",
        "calibration_eligible": "int",
        "certified_wcet": "int",
        "scalar_work_units": "int",
        "scalar_observed_ns_per_scan": "float",
        "scalar_observed_ns_per_work_unit": "float",
        "mixed_instructions": "int",
        "mixed_observed_ns_per_instruction": "float",
        "native_work_units": "int",
        "native_fb_instances": "int",
        "native_observed_ns_per_scan": "float",
    },
    "WINDOW_METRICS": {
        "segments": "int",
        "window_replan_us": "float",
    },
}

ORACLE_SPEC = {
    "domain": "safe",
    "attempted": "int",
    "planner_fail": "int",
    "oracle_miss": "int",
    "negative_fail": "int",
    "compared": "int",
    "excess_min": "int",
    "excess_max": "int",
    "excess_sum": "int",
    "hard_gate": "int",
}

TIMING_METRICS = [
    "cartesian_ik_cycle_us",
    "serial_chain_ik_us",
    "mixed_observed_ns_per_instruction",
    "window_replan_us",
]

WORKLOAD_IDENTITY_FIELDS = [
    "budget_us",
    "serial_chain_budget_us",
    "scalar_work_units",
    "native_work_units",
    "native_fb_instances",
    "mixed_instructions",
    "segments",
]


class ToolError(Exception):
    pass


def parse_args(argv):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    record = subparsers.add_parser("record")
    record.add_argument("--policy", required=True)
    record.add_argument("--benchmark", required=True)
    record.add_argument("--oracle", required=True)
    record.add_argument("--output", required=True)
    record.add_argument("--sha", default=None)

    compare = subparsers.add_parser("compare")
    compare.add_argument("--policy", required=True)
    compare.add_argument("--base-benchmark", required=True)
    compare.add_argument("--head-benchmark", required=True)
    compare.add_argument("--base-oracle", required=True)
    compare.add_argument("--head-oracle", required=True)
    compare.add_argument("--output", required=True)
    compare.add_argument("--base-sha", default=None)
    compare.add_argument("--head-sha", default=None)
    return parser.parse_args(argv)


def fail_payload(command, errors):
    return {
        "schema_version": SCHEMA_VERSION,
        "mode": command,
        "policy_schema_version": None,
        "verdict": {
            "status": "fail",
            "warnings": [],
            "errors": errors,
        },
    }


def read_policy(path):
    try:
        payload = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ToolError(f"failed to read policy: {exc}") from exc
    expect_exact_keys(
        payload,
        {"schema_version", "sample_pairs", "thresholds", "line_limits"},
        "policy",
    )
    if type(payload["schema_version"]) is not int or payload["schema_version"] != 1:
        raise ToolError(f"unsupported policy schema_version={payload['schema_version']}")
    if type(payload["sample_pairs"]) is not int or payload["sample_pairs"] != 9:
        raise ToolError("policy sample_pairs must be 9")
    expect_exact_keys(
        payload["thresholds"],
        {
            "warn_ratio",
            "fail_ratio_median",
            "fail_ratio_pair_min",
            "fail_ratio_pair_count",
        },
        "policy.thresholds",
    )
    expect_exact_keys(payload["line_limits"], {"max_length"}, "policy.line_limits")
    for key in ("warn_ratio", "fail_ratio_median", "fail_ratio_pair_min"):
        value = payload["thresholds"][key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(float(value)):
            raise ToolError(f"policy threshold {key} must be finite number")
    if (
        payload["thresholds"]["warn_ratio"] <= 1.0
        or payload["thresholds"]["fail_ratio_pair_min"] < payload["thresholds"]["warn_ratio"]
        or payload["thresholds"]["fail_ratio_median"] < payload["thresholds"]["fail_ratio_pair_min"]
    ):
        raise ToolError("policy threshold ordering invalid")
    pair_count = payload["thresholds"]["fail_ratio_pair_count"]
    if type(pair_count) is not int or pair_count < 1 or pair_count > payload["sample_pairs"]:
        raise ToolError("policy fail_ratio_pair_count out of range")
    max_length = payload["line_limits"]["max_length"]
    if type(max_length) is not int or max_length < 64 or max_length > 4096:
        raise ToolError("policy line_limits.max_length must be integer in [64, 4096]")
    return payload


def expect_exact_keys(payload, expected, label):
    if not isinstance(payload, dict):
        raise ToolError(f"{label} must be an object")
    actual = set(payload.keys())
    if actual != expected:
        unknown = sorted(actual - expected)
        missing = sorted(expected - actual)
        bits = []
        if unknown:
            bits.append(f"unknown={unknown}")
        if missing:
            bits.append(f"missing={missing}")
        raise ToolError(f"{label} field mismatch: {' '.join(bits)}")


def command_for_path(path):
    candidate = Path(path)
    if candidate.suffix.lower() == ".py":
        return [sys.executable, str(candidate)]
    return [str(candidate)]


def run_sample(path):
    try:
        completed = subprocess.run(
            command_for_path(path),
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
            timeout=120,
        )
    except subprocess.TimeoutExpired as exc:
        raise ToolError(f"command timed out after 120s: {path}") from exc
    except (OSError, UnicodeError) as exc:
        raise ToolError(f"failed to run command {path}: {exc}") from exc
    if completed.returncode != 0:
        raise ToolError(
            f"command failed: {path} exit={completed.returncode} stderr={completed.stderr.strip()}"
        )
    return completed.stdout


def parse_typed_value(key, raw, kind):
    if kind == "safe":
        if not SAFE_TOKEN.fullmatch(raw):
            raise ToolError(f"unsafe token for {key}: {raw}")
        return raw
    if kind == "version":
        if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", raw):
            raise ToolError(f"invalid version token for {key}: {raw}")
        return raw
    if kind == "int":
        if not re.fullmatch(r"-?[0-9]+", raw):
            raise ToolError(f"invalid integer token for {key}: {raw}")
        return int(raw)
    if kind == "float":
        try:
            value = float(raw)
        except ValueError as exc:
            raise ToolError(f"invalid float token for {key}: {raw}") from exc
        if not math.isfinite(value):
            raise ToolError(f"non-finite float token for {key}: {raw}")
        return value
    raise ToolError(f"unsupported token kind: {kind}")


def parse_metric_line(line, prefix, spec):
    tokens = line.split()
    if not tokens or tokens[0] != prefix:
        raise ToolError(f"line does not start with {prefix}: {line}")
    seen = {}
    for token in tokens[1:]:
        if "=" not in token:
            raise ToolError(f"invalid token in {prefix}: {token}")
        key, raw = token.split("=", 1)
        if key in seen:
            raise ToolError(f"duplicate token {key} in {prefix}")
        if key not in spec:
            raise ToolError(f"unknown token {key} in {prefix}")
        seen[key] = parse_typed_value(key, raw, spec[key])
    missing = [key for key in spec if key not in seen]
    if missing:
        raise ToolError(f"missing token(s) {missing} in {prefix}")
    if len(seen) != len(spec):
        raise ToolError(f"unexpected token count in {prefix}")
    return seen


def validate_lines(stdout, policy):
    max_length = int(policy["line_limits"]["max_length"])
    for line in stdout.splitlines():
        if len(line) > max_length:
            raise ToolError(f"line exceeds max_length={max_length}")


def parse_benchmark_stdout(stdout, policy):
    validate_lines(stdout, policy)
    parsed = {}
    for line in stdout.splitlines():
        if not line.strip():
            continue
        for prefix, spec in BENCHMARK_SPECS.items():
            if line.startswith(prefix + " "):
                if prefix in parsed:
                    raise ToolError(f"duplicate line {prefix}")
                parsed[prefix] = parse_metric_line(line, prefix, spec)
                break
    missing = [prefix for prefix in BENCHMARK_SPECS if prefix not in parsed]
    if missing:
        raise ToolError(f"benchmark output missing required line(s): {missing}")
    st = parsed["ST_WCET_METRICS"]
    validate_benchmark_metrics(parsed)
    return {
        "metrics": {
            **parsed["CARTESIAN_METRICS"],
            **parsed["SERIAL_CHAIN_METRICS"],
            **st,
            **parsed["WINDOW_METRICS"],
        },
        "environment": {
            "platform": st["platform"],
            "compiler": st["compiler"],
            "compiler_version": st["compiler_version"],
            "build": st["build"],
            "host_os": platform.system(),
            "host_arch": platform.machine(),
            "runner_image_os": os.getenv("ImageOS"),
            "runner_image_version": os.getenv("ImageVersion"),
        },
        "raw_stdout": stdout,
    }


def parse_oracle_stdout(stdout, policy):
    validate_lines(stdout, policy)
    domains = {}
    for line in stdout.splitlines():
        if not line.strip():
            continue
        if not line.startswith("OTG_ORACLE_METRICS "):
            continue
        parsed = parse_metric_line(line, "OTG_ORACLE_METRICS", ORACLE_SPEC)
        domain = parsed["domain"]
        if domain in domains:
            raise ToolError(f"duplicate oracle domain {domain}")
        domains[domain] = parsed
    if not domains:
        raise ToolError("oracle output missing OTG_ORACLE_METRICS lines")
    validate_oracle_domains(domains)
    return {
        "domains": domains,
        "raw_stdout": stdout,
    }


def require_positive(value, label):
    if value <= 0:
        raise ToolError(f"{label} must be > 0")


def require_non_negative(value, label):
    if value < 0:
        raise ToolError(f"{label} must be >= 0")


def validate_benchmark_metrics(parsed):
    cartesian = parsed["CARTESIAN_METRICS"]
    serial_chain = parsed["SERIAL_CHAIN_METRICS"]
    st = parsed["ST_WCET_METRICS"]
    window = parsed["WINDOW_METRICS"]
    require_positive(cartesian["cartesian_ik_cycle_us"], "cartesian_ik_cycle_us")
    require_positive(cartesian["budget_us"], "budget_us")
    require_positive(serial_chain["serial_chain_ik_us"], "serial_chain_ik_us")
    require_positive(serial_chain["serial_chain_budget_us"], "serial_chain_budget_us")
    if cartesian["budget_us"] != 50.0:
        raise ToolError("budget_us must remain 50")
    if serial_chain["serial_chain_budget_us"] != 30.0:
        raise ToolError("serial_chain_budget_us must remain 30")
    if st["calibration_eligible"] != 1:
        raise ToolError("calibration_eligible must be 1")
    if st["certified_wcet"] != 0:
        raise ToolError("certified_wcet must be 0")
    for key in (
        "scalar_work_units",
        "scalar_observed_ns_per_scan",
        "scalar_observed_ns_per_work_unit",
        "mixed_instructions",
        "mixed_observed_ns_per_instruction",
        "native_work_units",
        "native_fb_instances",
        "native_observed_ns_per_scan",
    ):
        require_positive(st[key], key)
    if window["segments"] != 64:
        raise ToolError("segments must be 64")
    require_positive(window["window_replan_us"], "window_replan_us")


def validate_oracle_domains(domains):
    for domain, metrics in domains.items():
        for key in ("attempted", "planner_fail", "oracle_miss", "negative_fail", "compared", "hard_gate"):
            require_non_negative(metrics[key], f"{domain}.{key}")
        if metrics["hard_gate"] not in (0, 1):
            raise ToolError(f"{domain}.hard_gate must be 0 or 1")
        if metrics["planner_fail"] + metrics["oracle_miss"] + metrics["negative_fail"] + metrics["compared"] != metrics["attempted"]:
            raise ToolError(f"{domain} attempted accounting mismatch")
        if metrics["compared"] == 0:
            if any(metrics[key] != 0 for key in ("excess_min", "excess_max", "excess_sum")):
                raise ToolError(f"{domain} excess metrics must be zero when compared=0")
            continue
        if metrics["excess_min"] > metrics["excess_max"]:
            raise ToolError(f"{domain} excess_min must be <= excess_max")
        if metrics["hard_gate"] == 1 and metrics["excess_min"] < 0:
            raise ToolError(f"{domain} hard-gated excess_min must be >= 0")
        if metrics["excess_sum"] < metrics["compared"] * metrics["excess_min"]:
            raise ToolError(f"{domain} excess_sum below compared*excess_min")
        if metrics["excess_sum"] > metrics["compared"] * metrics["excess_max"]:
            raise ToolError(f"{domain} excess_sum above compared*excess_max")


def collect_benchmark_sample(path, policy):
    stdout = run_sample(path)
    return parse_benchmark_stdout(stdout, policy)


def collect_oracle_sample(path, policy):
    stdout = run_sample(path)
    return parse_oracle_stdout(stdout, policy)


def compare_environment(base_env, head_env):
    mismatches = []
    for key in ("platform", "compiler", "compiler_version", "build", "host_os", "host_arch"):
        if base_env.get(key) != head_env.get(key):
            mismatches.append(f"{key}: {base_env.get(key)} != {head_env.get(key)}")
    return mismatches


def metric_ratios(base_metrics, head_metrics):
    ratios = {}
    for key in TIMING_METRICS:
        base_value = float(base_metrics[key])
        head_value = float(head_metrics[key])
        if base_value <= 0:
            raise ToolError(f"base metric must be > 0 for ratio: {key}")
        ratios[key] = head_value / base_value
    return ratios


def compare_workload_identity(base_metrics, head_metrics):
    mismatches = []
    for key in WORKLOAD_IDENTITY_FIELDS:
        if base_metrics.get(key) != head_metrics.get(key):
            mismatches.append(f"{key}: {base_metrics.get(key)} != {head_metrics.get(key)}")
    return mismatches


def collect_pairs(base_path, head_path, policy, sample_pairs):
    pairs = []
    for index in range(sample_pairs):
        order = "base-head" if index % 2 == 0 else "head-base"
        if order == "base-head":
            base_sample = collect_benchmark_sample(base_path, policy)
            head_sample = collect_benchmark_sample(head_path, policy)
        else:
            head_sample = collect_benchmark_sample(head_path, policy)
            base_sample = collect_benchmark_sample(base_path, policy)
        mismatches = compare_environment(base_sample["environment"], head_sample["environment"])
        if mismatches:
            raise ToolError(f"environment mismatch: {', '.join(mismatches)}")
        workload_mismatches = compare_workload_identity(
            base_sample["metrics"], head_sample["metrics"]
        )
        if workload_mismatches:
            raise ToolError(f"workload identity mismatch: {', '.join(workload_mismatches)}")
        pairs.append(
            {
                "pair_index": index + 1,
                "order": order,
                "base": base_sample,
                "head": head_sample,
                "ratios": metric_ratios(base_sample["metrics"], head_sample["metrics"]),
            }
        )
    return pairs


def summarize_pairs(pairs, policy):
    thresholds = policy["thresholds"]
    summary = {}
    confirmation_needed = set()
    warnings = []
    for metric in TIMING_METRICS:
        ratios = [pair["ratios"][metric] for pair in pairs]
        median_ratio = statistics.median(ratios)
        over_warn = sum(1 for value in ratios if value > thresholds["warn_ratio"])
        over_pair = sum(1 for value in ratios if value > thresholds["fail_ratio_pair_min"])
        needs_confirmation = (
            median_ratio > thresholds["fail_ratio_median"]
            and over_pair >= thresholds["fail_ratio_pair_count"]
        )
        if needs_confirmation:
            confirmation_needed.add(metric)
        elif median_ratio > thresholds["warn_ratio"]:
            warnings.append(f"{metric} median ratio={median_ratio:.3f}")
        summary[metric] = {
            "median_ratio": round(median_ratio, 6),
            "over_warn_pairs": over_warn,
            "over_fail_pairs": over_pair,
            "needs_confirmation": needs_confirmation,
        }
    return summary, sorted(confirmation_needed), warnings


def compare_otg(base_oracle, head_oracle):
    errors = []
    base_domains = set(base_oracle["domains"].keys())
    head_domains = set(head_oracle["domains"].keys())
    if base_domains != head_domains:
        errors.append(f"OTG domain set mismatch: {sorted(base_domains)} != {sorted(head_domains)}")
        return errors
    for domain in sorted(base_domains):
        base = base_oracle["domains"][domain]
        head = head_oracle["domains"][domain]
        if head["compared"] != base["compared"]:
            errors.append(f"OTG compared mismatch in {domain}: {base['compared']} != {head['compared']}")
        if head["attempted"] != base["attempted"]:
            errors.append(f"OTG attempted mismatch in {domain}: {base['attempted']} != {head['attempted']}")
        if head["hard_gate"] != base["hard_gate"]:
            errors.append(f"OTG hard_gate mismatch in {domain}: {base['hard_gate']} != {head['hard_gate']}")
        if head["planner_fail"] > base["planner_fail"]:
            errors.append(
                f"OTG planner_fail regressed in {domain}: {base['planner_fail']} -> {head['planner_fail']}"
            )
        if head["oracle_miss"] > base["oracle_miss"]:
            errors.append(
                f"OTG oracle_miss regressed in {domain}: {base['oracle_miss']} -> {head['oracle_miss']}"
            )
        if head["negative_fail"] > base["negative_fail"]:
            errors.append(
                f"OTG negative_fail regressed in {domain}: {base['negative_fail']} -> {head['negative_fail']}"
            )
        if head["excess_max"] > base["excess_max"]:
            errors.append(
                f"OTG excess_max regressed in {domain}: {base['excess_max']} -> {head['excess_max']}"
            )
        base_avg = 0.0 if base["compared"] == 0 else base["excess_sum"] / base["compared"]
        head_avg = 0.0 if head["compared"] == 0 else head["excess_sum"] / head["compared"]
        if head_avg > base_avg:
            errors.append(f"OTG avg_excess regressed in {domain}: {base_avg:.6f} -> {head_avg:.6f}")
    return errors


def verdict_summary(status, warnings, errors):
    if status == "fail":
        return f"benchmark trend FAIL: {'; '.join(errors)}"
    if status == "warn":
        return f"benchmark trend WARN: {'; '.join(warnings)}"
    return "benchmark trend PASS"


def write_output(path, payload):
    output_path = Path(path)
    try:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
    except (OSError, UnicodeError, TypeError, ValueError) as exc:
        raise ToolError(f"failed to write output: {exc}") from exc


def build_record_payload(args, policy):
    benchmark_sample = collect_benchmark_sample(args.benchmark, policy)
    oracle_sample = collect_oracle_sample(args.oracle, policy)
    return {
        "schema_version": SCHEMA_VERSION,
        "mode": "record",
        "policy_schema_version": policy["schema_version"],
        "policy": policy,
        "sha": args.sha,
        "environment": benchmark_sample["environment"],
        "benchmark": {
            "sample": benchmark_sample,
        },
        "oracle": {
            "sample": oracle_sample,
        },
        "verdict": {
            "status": "pass",
            "warnings": [],
            "errors": [],
        },
    }


def build_compare_payload(args, policy):
    collect_benchmark_sample(args.base_benchmark, policy)
    collect_benchmark_sample(args.head_benchmark, policy)
    collect_oracle_sample(args.base_oracle, policy)
    collect_oracle_sample(args.head_oracle, policy)

    base_oracle = collect_oracle_sample(args.base_oracle, policy)
    head_oracle = collect_oracle_sample(args.head_oracle, policy)
    otg_errors = compare_otg(base_oracle, head_oracle)

    initial_pairs = collect_pairs(
        args.base_benchmark,
        args.head_benchmark,
        policy,
        int(policy["sample_pairs"]),
    )
    initial_summary, confirmation_metrics, warnings = summarize_pairs(initial_pairs, policy)

    confirmation_pairs = []
    confirmation_summary = {}
    confirmation_errors = []
    if confirmation_metrics:
        confirmation_pairs = collect_pairs(
            args.base_benchmark,
            args.head_benchmark,
            policy,
            int(policy["sample_pairs"]),
        )
        confirmation_summary, _, _ = summarize_pairs(confirmation_pairs, policy)
        for metric in confirmation_metrics:
            if confirmation_summary[metric]["needs_confirmation"]:
                confirmation_errors.append(
                    f"{metric} median ratio remained above {policy['thresholds']['fail_ratio_median']}"
                )
            else:
                warnings.append(
                    f"{metric} initial regression confirmed as noisy after second pass"
                )

    errors = list(otg_errors)
    errors.extend(confirmation_errors)
    status = "pass"
    if errors:
        status = "fail"
    elif warnings:
        status = "warn"

    return {
        "schema_version": SCHEMA_VERSION,
        "mode": "compare",
        "policy_schema_version": policy["schema_version"],
        "policy": policy,
        "base_sha": args.base_sha,
        "head_sha": args.head_sha,
        "oracle": {
            "base": base_oracle,
            "head": head_oracle,
        },
        "benchmark": {
            "initial_pairs": initial_pairs,
            "initial_summary": initial_summary,
            "confirmation_pairs": confirmation_pairs,
            "confirmation_summary": confirmation_summary,
        },
        "verdict": {
            "status": status,
            "warnings": warnings,
            "errors": errors,
        },
    }


def main(argv=None):
    args = parse_args(argv)
    try:
        policy = read_policy(args.policy)
        if args.command == "record":
            payload = build_record_payload(args, policy)
        else:
            payload = build_compare_payload(args, policy)
    except ToolError as exc:
        payload = fail_payload(args.command, [str(exc)])
        try:
            write_output(args.output, payload)
        except ToolError:
            pass
        print(verdict_summary("fail", [], payload["verdict"]["errors"]), file=sys.stderr)
        return 1

    try:
        write_output(args.output, payload)
    except ToolError as exc:
        payload = fail_payload(args.command, [str(exc)])
        try:
            write_output(args.output, payload)
        except ToolError:
            pass
        print(verdict_summary("fail", [], payload["verdict"]["errors"]), file=sys.stderr)
        return 1
    print(
        verdict_summary(
            payload["verdict"]["status"],
            payload["verdict"]["warnings"],
            payload["verdict"]["errors"],
        ),
        file=sys.stderr,
    )
    return 1 if payload["verdict"]["status"] == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
