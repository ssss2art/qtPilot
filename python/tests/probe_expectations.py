"""Domain expectations shared by native and optional binding qualification tests."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class ProbeExpectation:
    reply: Mapping[str, object]

    def result(self) -> Mapping[str, object]:
        result = self.reply.get("result")
        assert isinstance(result, dict), f"Expected probe result envelope, got {self.reply!r}"
        return result

    def to_identify(self, *, application: str, qt_version: str) -> ProbeExpectation:
        result = self.result()
        assert result.get("pong") is True, f"Probe did not answer ping: {result!r}"
        assert result.get("appName") == application, f"Wrong process: {result!r}; expected {application!r}"
        assert result.get("qtVersion") == qt_version, f"Wrong Qt runtime: {result!r}; expected {qt_version!r}"
        pid = result.get("pid")
        assert isinstance(pid, int) and pid > 0, f"Missing process identity: {result!r}"
        return self

    def to_have_value(self, value: object) -> ProbeExpectation:
        result = self.result()
        assert "value" in result, f"Missing application value: {result!r}"
        assert result.get("value") == value, f"Expected application value {value!r}, got {result!r}"
        return self

    def to_find(self, object_name: str) -> str:
        objects = self.result().get("objects")
        assert isinstance(objects, list), f"Missing object discovery: {self.reply!r}"
        matching = [obj for obj in objects if isinstance(obj, dict) and obj.get("objectName") == object_name]
        assert len(matching) == 1, f"Expected one {object_name!r}, discovered {objects!r}"
        identifier = matching[0].get("objectId")
        assert isinstance(identifier, str) and identifier, f"Missing object handle: {matching[0]!r}"
        return identifier


def expect_probe(reply: Mapping[str, object]) -> ProbeExpectation:
    return ProbeExpectation(reply)
