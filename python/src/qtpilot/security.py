"""Explicit transport policy and credential handling at the connection boundary."""

from __future__ import annotations

import inspect
import ipaddress
import logging
import re
import ssl
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import SplitResult, urlsplit

from websockets.asyncio.client import connect as WebSocketConnect

from qtpilot.result import Err, Ok, Result

SUPPORTS_PROXY_OPTION = "proxy" in inspect.signature(WebSocketConnect).parameters


class ProbeConnector(WebSocketConnect):
    """An operator-selected probe endpoint must never redirect to another endpoint."""

    def process_redirect(self, exc: Exception) -> Exception:
        return exc


def validate_probe_url(url: str) -> SplitResult:
    try:
        parsed = urlsplit(url)
        valid = (parsed.scheme in {"ws", "wss"} and parsed.hostname and
                 not parsed.username and not parsed.password and not parsed.query and
                 not parsed.fragment and (parsed.port is None or 0 < parsed.port < 65536) and
                 not any(ord(char) <= 32 for char in url))
    except ValueError:
        valid = False
    if not valid:
        raise ValueError("Probe URL must be ws/wss with no credentials, query or fragment") from None
    return parsed


def _loopback(host: str) -> bool:
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def operating_profile(environment: Mapping[str, str]) -> str:
    profile = environment.get("QTPILOT_PROFILE", "")
    if profile not in {"", "local", "trusted-network", "remote"}:
        raise ValueError("Invalid operating profile")
    return profile


def local_probe_url(port: int, environment: Mapping[str, str]) -> str:
    """Choose an endpoint for a locally launched probe without downgrading its transport."""
    profile = operating_profile(environment)
    secure = "QTPILOT_TLS_CERT_FILE" in environment or profile in {"trusted-network", "remote"}
    host = "127.0.0.1" if profile or secure or "QTPILOT_AUTH_TOKEN_FILE" in environment else "localhost"
    return f"{'wss' if secure else 'ws'}://{host}:{port}"


def _read_token(path: str) -> Result[str, str]:
    try:
        with Path(path).open("rb") as stream:
            raw = stream.read(260)
    except OSError:
        return Err("Cannot read authentication credential file")
    raw = raw.removesuffix(b"\n").removesuffix(b"\r")
    if not re.fullmatch(rb"[A-Za-z0-9_-]{32,256}", raw):
        return Err("Invalid authentication credential format")
    return Ok(raw.decode("ascii"))


@dataclass(frozen=True, slots=True)
class ClientSecurity:
    profile: str
    token: str | None = field(repr=False)
    tls: ssl.SSLContext | None = field(repr=False)


def resolve_client_security(url: str, environment: Mapping[str, str]) -> Result[ClientSecurity, str]:
    endpoint = validate_probe_url(url)
    try:
        profile = operating_profile(environment)
    except ValueError as exc:
        return Err(str(exc))
    has_token = "QTPILOT_AUTH_TOKEN_FILE" in environment
    if profile in {"trusted-network", "remote"} and (not has_token or endpoint.scheme != "wss"):
        return Err("Operating profile requires authentication and TLS")
    if profile == "local" and not _loopback(endpoint.hostname or ""):
        return Err("Local profile requires a literal loopback endpoint")
    if has_token and endpoint.scheme != "wss" and not _loopback(endpoint.hostname or ""):
        return Err("Authentication requires verified TLS or a literal loopback endpoint")
    if "QTPILOT_TLS_CA_FILE" in environment and endpoint.scheme != "wss":
        return Err("Configured TLS trust cannot be used with plaintext WebSocket")
    tls = None
    if endpoint.scheme == "wss":
        try:
            tls = ssl.create_default_context(cafile=environment.get("QTPILOT_TLS_CA_FILE"))
            tls.minimum_version = ssl.TLSVersion.TLSv1_2
        except (OSError, ValueError):
            return Err("Cannot load TLS trust configuration")
    token: Result[str | None, str] = _read_token(environment["QTPILOT_AUTH_TOKEN_FILE"]) if has_token else Ok(None)
    return token.map(lambda value: ClientSecurity(profile, value, tls))


class _CredentialRedaction(logging.Filter):
    def __init__(self, token: str | None) -> None:
        super().__init__()
        self._token = token

    def _redact(self, text: str) -> str:
        if self._token:
            text = text.replace(self._token, "[redacted]")
        return re.sub(r"(?im)(authorization:)[^\r\n]*", r"\1 [redacted]", text)

    def filter(self, record: logging.LogRecord) -> bool:
        record.msg = self._redact(record.getMessage())
        record.args = ()
        if record.exc_info:
            record.exc_text = self._redact(logging.Formatter().formatException(record.exc_info))
            record.exc_info = None
        return True


def transport_logger(token: str | None) -> logging.Logger:
    # A session-owned logger avoids retaining credentials in the global registry.
    logger = logging.Logger("qtpilot.transport", logging.NOTSET)
    logger.parent = logging.getLogger("qtpilot")
    logger.addFilter(_CredentialRedaction(token))
    return logger
