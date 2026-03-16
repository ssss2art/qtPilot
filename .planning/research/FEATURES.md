# Feature Landscape: Qt Introspection/Automation Tools

**Domain:** Qt application introspection and AI-driven automation
**Researched:** 2026-01-29
**Context:** qtPilot - injection library enabling AI assistants to control Qt applications at runtime via MCP protocol

## Executive Summary

Qt introspection and automation tools span from debugging/development tools (GammaRay) to commercial test automation platforms (Squish, TestComplete, Ranorex) to lightweight testing frameworks (Qt Test). For qtPilot's AI automation use case, the feature set must bridge introspection capabilities with MCP-compatible interfaces across three API modes: Native, Computer Use, and Chrome (accessibility tree).

---

## Table Stakes

Features users expect. Missing = product feels incomplete or unusable.

| Feature | Why Expected | Complexity | Dependencies | Notes |
|---------|--------------|------------|--------------|-------|
| **QObject tree traversal** | Core Qt architecture - every tool does this | Low | Qt meta-object system | Via `QObject::children()`, `findChild()` |
| **Property read/write** | Fundamental introspection capability | Low | QMetaProperty | All competitors support this |
| **Signal/slot inspection** | Core Qt feature, debugging essential | Medium | QMetaMethod | Must list connections, not just definitions |
| **Widget geometry access** | Required for any UI automation | Low | QWidget API | `geometry()`, `pos()`, `size()` |
| **Screenshot capture** | Computer Use mode requires this | Low | QWidget::grab(), QQuickWindow | Must handle both Widgets and QML |
| **Mouse event simulation** | Basic automation requirement | Medium | QTest or manual event creation | Position-based clicks, drags |
| **Keyboard event simulation** | Basic automation requirement | Medium | QTest | Key press/release, text input |
| **Element identification by name** | Stable automation requires addressability | Medium | `objectName` property | Users expect findChild-like lookup |
| **JSON-RPC transport** | MCP protocol requirement | Low | Standard protocol | Already defined by MCP spec |
| **Process injection/attachment** | Tool must connect to target app | High | Platform-specific (DLL/LD_PRELOAD) | GammaRay, Squish, Qat all do this |
| **Cross-platform support** | Qt's core value proposition | Medium | CMake, platform abstraction | Windows, Linux minimum; macOS nice-to-have |
| **QML/Qt Quick support** | Modern Qt apps use QML | High | QQuickItem tree differs from QObject | Can't be Widgets-only in 2026 |

### Rationale for Table Stakes

1. **QObject introspection** - Every Qt introspection tool provides this. GammaRay, Squish, and Qt Test all access the meta-object system. Without this, you cannot navigate a Qt application.

2. **Property manipulation** - GammaRay allows live editing; Squish exposes all Q_PROPERTY values; this is fundamental to both debugging and automation.

3. **Event simulation** - Qt Test provides `QTest::mouseClick()`, `QTest::keyClick()`; Squish records/replays events; TestComplete simulates user input. Any automation tool must synthesize events.

4. **Screenshots** - Computer Use mode explicitly requires visual feedback. `QWidget::grab()` and `QQuickWindow::grabWindow()` are standard Qt APIs.

5. **Stable element identification** - Squish documentation extensively covers object identification strategies. Tests fail when elements can't be reliably found.

---

## Differentiators

Features that set qtPilot apart. Not expected, but create competitive advantage.

| Feature | Value Proposition | Complexity | Dependencies | Notes |
|---------|-------------------|------------|--------------|-------|
| **Accessibility tree export (Chrome mode)** | Enables AI-native interaction via refs, matches Claude's browser API | High | QAccessible, QAccessibleInterface | Unique - no Qt tool exposes this format |
| **MCP-native protocol** | Direct AI agent integration without translation layer | Medium | MCP SDK | Primary differentiator - purpose-built for AI |
| **Three API modes (Native/Computer Use/Chrome)** | Flexibility for different AI capabilities | High | All introspection subsystems | No competitor offers this versatility |
| **Real-time object change notifications** | AI can react to state changes | Medium | Signal monitoring | GammaRay has this; automation tools don't expose it |
| **Model/View data access** | Navigate QAbstractItemModel data programmatically | High | QModelIndex, roles | GammaRay has this; critical for data-driven apps |
| **State machine visualization** | AI can understand app state transitions | High | QStateMachine introspection | GammaRay exclusive feature |
| **Layout diagnostic overlay** | Visual debugging for AI to understand structure | Medium | QPainter overlay | GammaRay has this; useful for Computer Use |
| **Semantic element descriptions** | AI-friendly descriptions beyond raw properties | Medium | Custom metadata | No tool does this well today |
| **Action recording for learning** | AI can watch user, learn patterns | High | Event interception | Squish does this for tests; novel for AI learning |
| **Coordinate-to-element mapping** | Click coords -> QObject for Computer Use mode | Medium | Hit testing | Required for Computer Use; not in introspection tools |
| **Async operation support** | Long-running AI commands with progress | Medium | Qt async patterns | MCP doesn't specify this; opportunity for innovation |
| **QML context/binding access** | Deep QML debugging for AI | High | QQmlContext, QQmlBinding | GammaRay-level depth |

### Why These Differentiate

1. **Accessibility tree (Chrome mode)** - Claude's existing browser automation uses `refs` from accessibility trees. Exposing Qt's QAccessible hierarchy in this format enables seamless AI integration. No existing tool does this.

2. **MCP protocol** - Purpose-built for AI agents. Squish uses custom protocols for IDEs; GammaRay uses internal protocols. MCP is the emerging standard.

3. **Three API modes** - Covers:
   - **Native**: Full Qt introspection for capable AI agents
   - **Computer Use**: Screenshot + coordinates for visual AI
   - **Chrome**: Accessibility tree + refs for structured interaction
   This flexibility is unique.

4. **Model/View access** - QAbstractItemModel is used everywhere (tables, trees, lists). GammaRay can inspect these; exposing data to AI enables automation of data-heavy applications.

---

## Anti-Features

Features to explicitly NOT build. Common mistakes in this domain.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Image-based element recognition** | Brittle, slow, unreliable across themes/resolutions | Use Qt's native object model and accessibility APIs |
| **Coordinate-only automation** | Breaks on resize, DPI changes, layouts | Use object references; coords only as fallback |
| **Recording-only test creation** | Squish/TestComplete pain point - recorded tests are hard to maintain | Expose structured API for AI to build understanding |
| **Blocking synchronous API** | Qt apps freeze; AI agents timeout | Use async JSON-RPC; return immediately with task IDs |
| **Custom scripting language** | Maintenance burden, learning curve | Use JSON-RPC; let AI use its native reasoning |
| **Monolithic probe** | Performance overhead, deployment complexity | Plugin architecture; load only needed modules |
| **Widget-only support** | Modern Qt is QML-heavy | Support both from day one; QML is not optional |
| **Global event hooks** | Performance killer, security concern | Targeted introspection on specific objects |
| **Full property serialization** | Memory explosion on complex object graphs | Lazy loading; serialize on-demand |
| **Automatic UI modification** | Dangerous without explicit consent | Read-only by default; write requires explicit call |
| **Window-level screenshots only** | Misses widget-specific context | Support both window and element-level capture |
| **Hard-coded timeouts** | Different operations need different waits | Configurable timeouts; async completion signals |
| **Relying on objectName everywhere** | Many apps don't set names | Multiple identification strategies (path, type, index) |
| **Single-app focus** | Enterprise apps have multiple processes | Support multi-process inspection from start |

### Anti-Pattern Deep Dives

**Image-based recognition** - Squish explicitly touts "object-based recognition, not image matching" as a key advantage. OCR is a last resort. Qt provides rich object metadata; use it.

**Blocking API** - Qt GUI thread must not block. If probe code blocks the main thread, the app becomes unresponsive. GammaRay uses separate threads; MCP should use async patterns.

**Widget-only** - Qt Quick 2 has been the recommended UI framework since Qt 5. Any modern introspection tool must handle QQuickItem trees, scene graphs, and QML bindings.

**Coordinate-only** - Squish documentation dedicates significant space to stable object identification because coordinate-based tests are fragile. The lesson applies to AI automation.

---

## Feature Dependencies

```
Process Injection (platform-specific)
    |
    v
QObject Tree Access
    |
    +---> Property Read/Write
    |         |
    |         v
    |     Signal/Slot Monitoring
    |
    +---> Widget Geometry --> Screenshot Capture
    |                              |
    |                              v
    |                         Computer Use Mode
    |
    +---> Accessibility Tree --> Chrome Mode (refs)
    |
    +---> QML Item Tree --> QML Context/Bindings
    |
    +---> Model/View Access --> Data Navigation

Event Simulation (requires tree access for targeting)
    |
    +---> Mouse Events
    +---> Keyboard Events
    +---> Touch Events (future)

JSON-RPC Transport (parallel track)
    |
    v
MCP Protocol Layer
    |
    +---> Native Mode API
    +---> Computer Use Mode API
    +---> Chrome Mode API
```

### Dependency Notes

1. **Injection first** - Nothing works without getting code into the target process
2. **QObject tree is foundation** - All introspection builds on this
3. **Three modes can be developed in parallel** once core introspection exists
4. **Accessibility tree requires QAccessible setup** - some apps don't enable it; need fallback

---

## MVP Recommendation

For MVP, prioritize these features in order:

### Phase 1: Foundation (Table Stakes Core)
1. Process injection (Windows DLL, Linux LD_PRELOAD)
2. QObject tree traversal
3. Property read/write
4. Basic element identification (by objectName, by path)
5. JSON-RPC/MCP transport skeleton

### Phase 2: Native Mode (Primary Differentiator)
6. Signal/slot inspection
7. Widget geometry access
8. Mouse/keyboard event simulation
9. QML item tree support
10. Full Native Mode API

### Phase 3: Computer Use Mode
11. Screenshot capture (widget and window level)
12. Coordinate-to-element mapping
13. Computer Use Mode API

### Phase 4: Chrome Mode
14. Accessibility tree export
15. Ref-based element interaction
16. Chrome Mode API

### Defer to Post-MVP
- State machine visualization (complex, niche)
- Model/View data navigation (important but can wait)
- Action recording (valuable but complex)
- QML binding/context deep inspection (advanced debugging)
- Multi-process support (enterprise feature)
- Layout diagnostic overlays (debugging aid)

---

## Complexity Estimates

| Feature Category | Complexity | Effort Estimate | Risk Level |
|-----------------|------------|-----------------|------------|
| Process injection | High | 2-3 weeks | High (platform-specific) |
| QObject introspection | Low | 1 week | Low (well-documented APIs) |
| Property system | Low | 3-5 days | Low |
| Event simulation | Medium | 1-2 weeks | Medium (edge cases) |
| Screenshot capture | Low | 3-5 days | Low |
| Accessibility tree | High | 2-3 weeks | Medium (Qt version variance) |
| QML support | High | 2-3 weeks | Medium |
| MCP protocol | Medium | 1-2 weeks | Low (spec exists) |
| Model/View access | High | 2 weeks | Medium |
| State machine viz | High | 2 weeks | Low (reference impl exists) |

---

## Confidence Assessment

| Area | Confidence | Rationale |
|------|------------|-----------|
| Table stakes features | HIGH | Consistent across all researched tools (GammaRay, Squish, Qt Test) |
| Differentiators | MEDIUM | MCP integration is novel; accessibility tree approach is sound but untested at scale |
| Anti-features | HIGH | Well-documented pain points in Squish documentation and community feedback |
| Complexity estimates | MEDIUM | Based on Qt API complexity and GammaRay's open-source implementation |
| Dependencies | HIGH | Standard Qt architecture; well-understood |

---

## Sources

### Primary (HIGH confidence)
- [GammaRay GitHub Repository](https://github.com/KDAB/GammaRay)
- [GammaRay User Manual](https://docs.kdab.com/gammaray-manual/latest/)
- [Qt Accessibility Documentation](https://doc.qt.io/qt-6/qaccessible.html)
- [Qt Test Overview](https://doc.qt.io/qt-6/qtest-overview.html)
- [Squish Object Identification](https://doc.qt.io/squish/improving-object-identification.html)
- [QWidget::grab() Documentation](https://doc.qt.io/qt-6/qwidget.html#grab)
- [QTest Event Simulation](https://doc.qt.io/qt-6/qttestlib-tutorial3-example.html)

### Secondary (MEDIUM confidence)
- [Squish for Qt Product Page](https://www.qt.io/quality-assurance/squish/platform-qt-gui-test-automation)
- [MCP Protocol Specification](https://modelcontextprotocol.io/specification/2025-06-18)
- [Qat Architecture Documentation](https://qat.readthedocs.io/en/stable/doc/contributing/Architecture.html)
- [Qt GUI Testing Pitfalls](https://www.qt.io/quality-assurance/blog/top-5-gui-testing-pitfalls-enterprise-teams-must-address-and-how-to-do-it)

### Tertiary (LOW confidence - general patterns)
- [Test Automation Anti-Patterns](https://www.testdevlab.com/blog/5-test-automation-anti-patterns-and-how-to-avoid-them)
- Forum discussions on Qt DLL injection and event simulation
