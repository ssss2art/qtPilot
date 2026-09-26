# Local test-run evidence

Collect test, build, probe, and red/green output under this directory. Generated
contents are ignored by Git; this README is the only automatic exception.

Use one directory per execution:

```text
logs/<UTC timestamp>-<behavior>-<red|green|baseline>/
  command.txt
  revision.txt
  changes.patch
  environment.txt
  console.log
  exit-code.txt
  junit.xml
```

Capture the exact command, working directory, revision, relevant uncommitted
changes, interpreter/compiler/Qt/binding versions, stdout/stderr, and real exit
code. Include structured reports and probe traces when relevant. For a red/green
pair, record which failing assertion proves the requirement and link its green
run. A loader, permission, or setup failure is not a behavioral red. Never
overwrite a red run with green output or let a logging pipeline hide test failure.
Keep environment capture narrowly scoped; do not dump the full environment.

Example from the repository root (Bash or Zsh, with `errexit` disabled):

```bash
run_dir="logs/$(date -u +%Y%m%dT%H%M%SZ)-signal-wait-baseline"
mkdir -p "$run_dir"
git rev-parse HEAD > "$run_dir/revision.txt"
git diff > "$run_dir/changes.patch"
printf 'cwd: %s\n' "$PWD" > "$run_dir/environment.txt"
python/.venv/bin/python --version >> "$run_dir/environment.txt" 2>&1
printf 'python/.venv/bin/pytest -q -p no:cacheprovider python/tests/test_signal_wait.py --junitxml="%s/junit.xml"\n' "$run_dir" > "$run_dir/command.txt"
test_exit=0
python/.venv/bin/pytest -q -p no:cacheprovider python/tests/test_signal_wait.py \
  --junitxml="$run_dir/junit.xml" > "$run_dir/console.log" 2>&1 || test_exit=$?
printf '%s\n' "$test_exit" > "$run_dir/exit-code.txt"
```

Inspect `exit-code.txt` and the output to report the result. In an automated
runner, return the saved exit code after capturing the artifacts. C++ build and
CTest runs follow the same convention using the appropriate Qt environment.
`git diff` does not include untracked files; note relevant fixture additions and
preserve their source separately when necessary for reproduction.

## Deliberately tracking evidence

Routine runs stay local. Track an artifact only when it explains a defect,
demonstrates a meaningful red/green result, or provides durable regression
evidence worth maintaining. Prefer a minimal synthetic reproduction and a small
excerpt over a full session dump.

Before tracking, inspect the exact artifact and remove secrets, credentials,
private endpoints, identifying paths, driven application/product names, and
screenshots that violate the project's confidentiality rule. Document any
redaction, the producing command/revision, why retention helps, and the limit
of what the evidence proves. Never claim a sanitized excerpt is the untouched
original. Do not add a broad ignore exception for a whole run directory.

Stage only the selected reviewed files explicitly, for example:

```bash
git add -f -- logs/<run-directory>/reviewed-evidence.log
```

Force-adding an artifact is a deliberate retention decision, not routine test
cleanup. Once tracked, Git continues tracking later changes despite ignore
rules: keep retained evidence immutable and write subsequent runs to new
directories. No generated evidence is being selected for tracking by this setup.
