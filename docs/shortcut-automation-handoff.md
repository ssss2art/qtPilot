# Shortcut automation handoff

## Problem

Qt applications commonly expose zoom and help shortcuts through both `QShortcut` and QML
`Shortcut`:

- platform-standard Zoom In and Zoom Out
- reset zoom (`Ctrl+0`)
- shortcut help (`?`)

The public `cu.key` API supports one modifier combination, but its `+`-separated grammar could not
express a literal plus key. It also lacked a portable name for the question-mark key.

## Implementation

Add these case-insensitive key names to `KeyNameMapper`:

- `Plus` -> `Qt::Key_Plus`
- `Minus` -> `Qt::Key_Minus`
- `Question` and `QuestionMark` -> `Qt::Key_Question`

Add `cmd` and `command` as aliases for `Qt::MetaModifier`.

Keep `+` as the separator. Callers should use named punctuation instead of escaping a literal
separator or relying on keyboard-layout assumptions such as `Shift+=`.

Examples:

```text
meta+Plus
ctrl+Minus
QuestionMark
```

## Acceptance coverage

Parser tests should verify the aliases and preserve existing combinations. Integration tests should
exercise the public `cu.key` path against:

- QWidget `QShortcut` instances
- QML `Shortcut` instances through the active `QWindow`

Each path should cover `meta+Plus`, `ctrl+Minus`, and `QuestionMark`.

## API boundary

`qt.methods.invoke` accepts Qt slots and `Q_INVOKABLE` methods. It intentionally does not emit
signals, so invoking a shortcut's `activated` signal is not an automation workaround. Shortcut
coverage should use the input path through `cu.key`.

## Remaining non-goals

- arbitrary signal emission
- raw key-down/key-up APIs
- multi-chord sequences such as `Ctrl+K, Ctrl+C`
- physical-key or keyboard-layout emulation
