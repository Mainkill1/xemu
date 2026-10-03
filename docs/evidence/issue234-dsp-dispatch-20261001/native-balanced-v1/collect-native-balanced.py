#!/usr/bin/env python3
"""Collect every frozen native attempt through the maintained runner client.

No launches, retries, outcome changes, or eligibility filtering occur here.
Refuse collection until all planned attempts are archived and the tester is idle.
Interrupted downloads can resume through the client's existing range protocol.
"""

import argparse
import json
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--runner-scripts", type=Path, required=True)
    parser.add_argument("--url", default="http://10.0.0.123:9368")
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.runner_scripts.resolve()))
    from runner_transport import ClientError, RunnerApi, inside
    from runner_workflows import Workflows

    campaign = args.campaign.resolve()
    plan = json.loads((campaign / "plan.json").read_text())
    attempts = plan["attempts"]
    ids = [attempt["id"] for attempt in attempts]
    if not ids or len(ids) != len(set(ids)):
        parser.error("Frozen plan must contain distinct attempt IDs.")
    api = RunnerApi(args.url)
    workflows = Workflows(api)

    def idle():
        status = api.json("/api/v1/status")
        if (status["Phase"] != "idle" or status["CurrentJob"] is not None
                or status["Queue"]["Testing"] or status["Queue"]["Pending"]):
            raise ClientError("tester_busy", "Collection requires an idle tester with an empty queue.")

    def save_once(path, value):
        if path.exists():
            if json.loads(path.read_text()) != value:
                raise ClientError("receipt_changed", f"Retained receipt differs: {path.name}")
        else:
            with path.open("x") as output:
                output.write(json.dumps(value, indent=2) + "\n")

    try:
        # Inspect every attempt before creating outputs or downloading payloads.
        results = []
        pending = []
        for identifier in ids:
            result = workflows.result(identifier)
            results.append(result)
            if result["state"] != "tested" or not result.get("available"):
                pending.append({"id": identifier, "state": result["state"]})
        if pending:
            print(json.dumps({"ready": False, "pending": pending}))
            return 2
        run_ids = [result["runId"] for result in results]
        if len(set(run_ids)) != len(run_ids):
            raise ClientError("run_identity_duplicate", "Distinct attempts must have distinct archived runs.")
        idle()
        if args.check_only:
            print(json.dumps({"ready": True, "attempts": len(results), "downloaded": 0}))
            return 0
        for result in results:
            idle()
            identifier = result["jobId"]
            save_once(inside(campaign, identifier + "-collection-result.json"), result)
            receipt = workflows.collect(identifier, campaign / "collected", [], True)
            if receipt["runId"] != result["runId"] or not receipt["complete"]:
                raise ClientError("collection_identity", "Collection is incomplete or belongs to another run.")
            save_once(inside(campaign, identifier + "-collection-receipt.json"), receipt)
            print(json.dumps({"id": identifier, "outcome": result["outcome"], "collection": receipt}), flush=True)
        idle()
        print(json.dumps({"collected": len(results), "scope": "all planned attempts; all eligible artifacts",
                          "rawAudit": "pending; run summarize-native-balanced.py separately"}))
        return 0
    except ClientError as error:
        print(json.dumps(error.document()), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
