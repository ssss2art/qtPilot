"""Token-efficient compact outline formatter for Qt object trees."""

from __future__ import annotations


def format_compact_tree(
    tree: dict,
    indent: int = 0,
    visible_only: bool = False,
) -> str:
    """Render a Qt object tree dictionary into an indented, token-efficient text outline.

    Args:
        tree: Nested object tree dictionary returned by qt.objects.tree.
        indent: Current indentation level (0 for top-level).
        visible_only: When True, omit hidden objects and their children.

    Returns:
        A compact newline-separated string representing the object hierarchy.
    """
    if not isinstance(tree, dict) or not tree:
        return "<empty tree>"

    # Unwrap {"result": ...} if present in raw envelope
    if "result" in tree and isinstance(tree["result"], dict) and len(tree) <= 2:
        tree = tree["result"]

    # Handle dummy Root container
    if tree.get("className") == "Root" and not tree.get("id"):
        children = tree.get("children", [])
        if visible_only:
            children = [c for c in children if c.get("visible", True) is not False]
        lines = [format_compact_tree(c, indent=0, visible_only=visible_only) for c in children]
        lines = [l for l in lines if l]
        return "\n".join(lines) if lines else "<empty tree>"

    if visible_only and tree.get("visible") is False:
        return ""

    prefix = "  " * indent
    parts: list[str] = []

    name = tree.get("objectName")
    cls = tree.get("className", "QObject")
    if name:
        parts.append(f"{name} ({cls})")
    else:
        parts.append(f"({cls})")

    geom = tree.get("geometry")
    if isinstance(geom, dict) and "width" in geom and "height" in geom:
        parts.append(f"[{geom['width']}x{geom['height']}]")

    text = tree.get("text")
    if text:
        clean_text = str(text).replace("\n", " ").strip()
        if len(clean_text) > 40:
            clean_text = clean_text[:37] + "..."
        parts.append(f'"{clean_text}"')

    if tree.get("visible") is False:
        parts.append("(hidden)")

    if tree.get("isQmlItem"):
        qml_id = tree.get("qmlId")
        qml_type = tree.get("qmlTypeName")
        if qml_id:
            parts.append(f"qml:{qml_id}")
        elif qml_type:
            parts.append(f"qmlType:{qml_type}")

    obj_id = tree.get("id")
    if obj_id:
        parts.append(f"#{obj_id}")

    lines = [f"{prefix}{' '.join(parts)}"]

    children = tree.get("children", [])
    if visible_only:
        children = [c for c in children if c.get("visible", True) is not False]

    for child in children:
        child_str = format_compact_tree(child, indent=indent + 1, visible_only=visible_only)
        if child_str:
            lines.append(child_str)

    return "\n".join(lines)


def prune_hidden_nodes(tree: dict) -> dict:
    """Filter out hidden objects and their children from a nested tree dictionary.

    Args:
        tree: Nested object tree dictionary.

    Returns:
        Pruned dictionary preserving the same shape as qt.objects.tree.
    """
    if not isinstance(tree, dict) or not tree:
        return {}

    # Unwrap envelope if present
    is_wrapped = False
    inner = tree
    if "result" in tree and isinstance(tree["result"], dict) and len(tree) <= 2:
        inner = tree["result"]
        is_wrapped = True

    def _prune(node: dict) -> dict | None:
        if node.get("visible") is False:
            return None
        new_node = dict(node)
        children = node.get("children", [])
        if children:
            pruned_children = []
            for child in children:
                res = _prune(child)
                if res is not None:
                    pruned_children.append(res)
            new_node["children"] = pruned_children
        return new_node

    if inner.get("className") == "Root" and not inner.get("id"):
        new_root = dict(inner)
        pruned_children = []
        for child in inner.get("children", []):
            res = _prune(child)
            if res is not None:
                pruned_children.append(res)
        new_root["children"] = pruned_children
        return {"result": new_root} if is_wrapped else new_root

    pruned = _prune(inner)
    if pruned is None:
        return {}
    return {"result": pruned} if is_wrapped else pruned
