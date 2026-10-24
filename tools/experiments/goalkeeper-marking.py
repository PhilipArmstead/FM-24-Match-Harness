"""Test every Marking value for home team's goalkeeper.

Place this script, baseline.json, and fm24ctl.exe in the same directory, then run:

    python -I goalkeeper-marking.py

For each value in 1-20, a copy of baseline.json is written to
baseline-marking-<i>.json with the home team's goalkeeper's stored
marking rating set to the corresponding displayed FM value, and:

    fm24ctl.exe run baseline-marking-<i>.json 42 marking-<i>.csv --detailed

is executed. baseline.json itself is never modified.
"""

import json
import subprocess
import time
from pathlib import Path

DIRECTORY = Path(__file__).resolve().parent
FIXTURE = DIRECTORY / "villas-souped.json"
FM24CTL = DIRECTORY / "fm24ctl.exe"
PLAYER_FIELD = "home.player[0].ratings"


def load_fixture():
    with FIXTURE.open(encoding="utf-8") as f:
        return json.load(f)


def get_player_ratings(fixture):
    for field in fixture["fields"]:
        if field.get("name") == PLAYER_FIELD:
            ratings = field.get("u8map")
            if not isinstance(ratings, dict):
                raise ValueError(f"{PLAYER_FIELD}: expected u8map ratings")
            return ratings
    raise ValueError(f"Could not find {PLAYER_FIELD}")


def write_variant(fixture, marking):
    ratings = get_player_ratings(fixture)
    ratings["marking"] = marking * 5

    variant = FIXTURE.with_name(f"{FIXTURE.stem}-marking-{marking}.json")
    with variant.open("w", encoding="utf-8") as f:
        json.dump(fixture, f, ensure_ascii=False)
        f.write("\n")
    return variant


def run_fixture(variant, marking):
    output = f"marking-{marking}.csv"
    output_path = DIRECTORY / output

    if output_path.exists():
        print(f"  -> {output} already exists, skipping", flush=True)
        return output

    subprocess.run(
        [
            str(FM24CTL),
            "run",
            variant.name,
            "1-5",
            output,
            "--detailed",
        ],
        cwd=DIRECTORY,
        check=True,
    )
    return output


def main():
    if not FIXTURE.is_file():
        raise FileNotFoundError(f"baseline.json not found beside the script: {FIXTURE}")
    if not FM24CTL.is_file():
        raise FileNotFoundError(f"fm24ctl.exe not found beside the script: {FM24CTL}")

    original = load_fixture()
    get_player_ratings(original)

    print(
        f"Starting Marking experiment: {len(range(1, 21))} runs",
        flush=True,
    )

    started = time.perf_counter()

    for marking in range(1, 21):
        fixture = json.loads(json.dumps(original))
        variant = write_variant(fixture, marking)

        print(
            f"Running marking={marking} ({variant.name})",
            flush=True,
        )
        output = run_fixture(variant, marking)
        print(f"  -> {output}", flush=True)

    print(
        f"Experiment took {time.perf_counter() - started:.2f} seconds",
        flush=True,
    )


if __name__ == "__main__":
    main()
