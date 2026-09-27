# Qt for Python fixture qualification

The controller stays Python and the injected probe stays C++. PySide is an
optional application fixture dependency, isolated in
[`python/tests/pyside`](../python/tests/pyside/pyproject.toml). Its lock does not
change the controller's dependencies. Fluent expectations live separately in
[`probe_expectations.py`](../python/tests/probe_expectations.py).

## Decision on 2026-09-26

Keep native C++ fixtures as the acceptance backend for the hardening work.
The local PySide experiment has partial success, but is **not qualified** for
slot/checkpoint fixtures. Do not infer general PySide or PyQt support from it.

| Tested combination | Result |
| --- | --- |
| macOS arm64; Python 3.14.7; PySide 6.11.2; probe built with Homebrew Qt 6.11.2 | Ping identifies the fixture process; object discovery succeeds; a Python-defined integer property reads as `1`, writes through the probe, and reads back as `42` |
| Loaded runtime | One QtCore image, from the probe's Homebrew Qt runtime; Python binding and Qt versions are both `6.11.2`; plugin paths and loaded QtCore paths captured by the fixture |
| Typed Python slot `increment(int)` through `qt.methods.invoke` | Fails with probe error `-32021`, `Method invocation failed: increment`; Qt reports `too few arguments (1)`; the acceptance expectation does not pass |
| Disconnect and teardown | Tests disconnect the client; process group receives termination; launcher is reaped; teardown checks it has exited |

The executable qualification suite reports **one pass and one failure** for this
combination. Equal Qt version strings and a single loaded QtCore image are
insufficient proof of complete compatibility. The slot failure's cause has not
been established. Linux, Windows, signals, queued callbacks, object destruction,
models and QML have not been qualified. The planned initial Linux combination
remains follow-up work; the available local macOS runtime was used for diagnosis.

## Reproduce

Build the native probe/launcher first with a Qt kit matching the fixture runtime.
The current isolated fixture lock pins PySide Essentials `6.11.2`. Do not reuse
an arbitrary differently built probe; qtPilot uses private Qt headers.

```bash
uv sync --project python/tests/pyside --locked --python 3.14
QTPILOT_PYSIDE_QUALIFY=1 \
QTPILOT_PYSIDE_PYTHON="$PWD/python/tests/pyside/.venv/bin/python" \
QTPILOT_TEST_BUILD_DIR="$PWD/build" \
python/.venv/bin/pytest python/tests/test_pyside_qualification.py -v
```

Save stdout/stderr and the exit code beneath the ignored [`logs/`](../logs/README.md)
directory. The tests print runtime/library/plugin diagnostics and process output.
Missing bindings, incompatible loading, missing binaries or a broken behavior
fail an explicitly requested qualification run. Ordinary controller tests skip
this opt-in suite, so Python-only development does not require Qt bindings.
There is no required hosted PySide job yet: its qualification is incomplete.

Resolve the typed-slot failure with a native reproducer and red/green evidence
before expanding the fixture. Then qualify signal arguments, queued completion,
destruction, model/reset and QML behavior individually. Native Widgets/QML E2E
remain required in the representative Linux build throughout that exploration.
