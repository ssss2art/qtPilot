"""WebSocket + JSON-RPC client for communicating with the qtPilot probe."""

from __future__ import annotations

import asyncio
import json
import logging
import os
import time
import warnings
from collections.abc import Callable

from websockets.asyncio.client import connect

logger = logging.getLogger(__name__)

# Default per-request deadline, in seconds.
#
# call() previously awaited its response future with no timeout at all, so a
# probe handler that blocked -- and every handler runs on the host app's GUI
# thread, so "blocked" includes a modal dialog or a long paint -- hung the MCP
# call forever with no diagnostic. The websockets ping_timeout does not help:
# the transport stays healthy while the handler is stuck.
#
# 30s is chosen to sit above the slowest legitimate call observed (a full
# qt.objects.tree over a large widget hierarchy) while still failing inside a
# human's patience. Override per call, or globally with QTPILOT_CALL_TIMEOUT.
# The wire-protocol revision this client speaks. Must match kProtocolVersion in
# src/probe/core/version.h.in. Bumped only when a change would make one side
# misread the other -- not on every release.
SUPPORTED_PROTOCOL_VERSION = 1

DEFAULT_CALL_TIMEOUT = 30.0

# Sentinel: lets call(timeout=None) mean "wait forever" while an omitted
# argument still picks up the configured default.
_UNSET: float = -1.0


def _default_call_timeout() -> float | None:
    """Resolve the default request deadline, honouring QTPILOT_CALL_TIMEOUT.

    A value of 0 or a negative number disables the deadline, which is the escape
    hatch for deliberately long-running debugging sessions.
    """
    raw = os.environ.get("QTPILOT_CALL_TIMEOUT")
    if raw is None or raw.strip() == "":
        return DEFAULT_CALL_TIMEOUT
    try:
        value = float(raw)
    except ValueError:
        logger.warning(
            "QTPILOT_CALL_TIMEOUT=%r is not a number; using %.1fs", raw, DEFAULT_CALL_TIMEOUT
        )
        return DEFAULT_CALL_TIMEOUT
    return value if value > 0 else None


class ProbeError(Exception):
    """Error returned by the qtPilot probe via JSON-RPC."""

    def __init__(self, message: str, code: int = -1, data: object = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.data = data

    @classmethod
    def from_jsonrpc(cls, error: dict) -> ProbeError:
        """Construct from a JSON-RPC error object."""
        return cls(
            message=error.get("message", "Unknown probe error"),
            code=error.get("code", -1),
            data=error.get("data"),
        )


class ProbeTimeoutError(ProbeError):
    """A request was sent but the probe did not respond within the deadline.

    Subclasses ProbeError so existing callers that catch ProbeError keep working;
    catch this specifically to distinguish "the probe is wedged" from "the probe
    rejected the call".
    """

    def __init__(self, method: str, timeout: float):
        super().__init__(
            f"Probe did not respond to {method!r} within {timeout:g}s. "
            "The handler may be blocked on the application's GUI thread.",
            code=-32000,
        )
        self.method = method
        self.timeout = timeout


class ProbeConnection:
    """Manages a WebSocket connection to the qtPilot probe.

    Sends JSON-RPC 2.0 requests and correlates responses by ID.
    """

    def __init__(self, ws_url: str) -> None:
        self._ws_url = ws_url
        self._ws = None
        self._next_id = 1
        self._pending: dict[int, asyncio.Future] = {}
        self._recv_task: asyncio.Task | None = None
        self._connected = False
        self._notification_handlers: list[Callable[[str, dict], None]] = []
        self._call_observers: list[Callable] = []
        self._send_observers: list[Callable] = []
        self._notification_queue: asyncio.Queue = asyncio.Queue(maxsize=10000)
        self._notification_task: asyncio.Task | None = None
        self._notification_drops: int = 0
        self._probe_version: str | None = None
        self._probe_protocol_version: int | None = None

    @property
    def is_connected(self) -> bool:
        """Whether the WebSocket connection is currently active."""
        return self._connected

    @property
    def probe_version(self) -> str | None:
        """The probe's reported build version, or None before the handshake."""
        return self._probe_version

    @property
    def probe_protocol_version(self) -> int | None:
        """The probe's reported wire-protocol revision, or None before the handshake."""
        return self._probe_protocol_version

    @property
    def ws_url(self) -> str:
        """The WebSocket URL this connection targets."""
        return self._ws_url

    def on_notification(self, handler: Callable[[str, dict], None] | None) -> None:
        """Register or unregister a callback for JSON-RPC notifications.

        .. deprecated:: Use add_notification_handler / remove_notification_handler.
            This method clears ALL existing handlers before setting the new one.
        """
        warnings.warn(
            "on_notification() is deprecated. Use add_notification_handler() / "
            "remove_notification_handler() instead.",
            DeprecationWarning,
            stacklevel=2,
        )
        self._notification_handlers.clear()
        if handler is not None:
            self._notification_handlers.append(handler)

    def add_notification_handler(self, handler: Callable[[str, dict], None]) -> None:
        """Add a notification handler. Multiple handlers can coexist."""
        self._notification_handlers.append(handler)

    def remove_notification_handler(self, handler: Callable[[str, dict], None]) -> None:
        """Remove a previously added notification handler. No-op if not found."""
        try:
            self._notification_handlers.remove(handler)
        except ValueError:
            pass

    def add_call_observer(self, observer: Callable) -> None:
        """Add a call observer. Called with (request, result_or_exc, duration_ms) after completion."""
        self._call_observers.append(observer)

    def remove_call_observer(self, observer: Callable) -> None:
        """Remove a call observer. No-op if not found."""
        try:
            self._call_observers.remove(observer)
        except ValueError:
            pass

    def add_send_observer(self, observer: Callable) -> None:
        """Add a send observer. Called with (request) when a request is sent."""
        self._send_observers.append(observer)

    def remove_send_observer(self, observer: Callable) -> None:
        """Remove a send observer. No-op if not found."""
        try:
            self._send_observers.remove(observer)
        except ValueError:
            pass

    def _notify_call_observers(
        self, request: dict, result_or_exc: dict | Exception, duration_ms: float
    ) -> None:
        """Notify all call observers safely."""
        for observer in list(self._call_observers):
            try:
                observer(request, result_or_exc, duration_ms)
            except Exception:
                logger.debug("Call observer error", exc_info=True)

    def _notify_send_observers(self, request: dict) -> None:
        """Notify all send observers safely."""
        for observer in list(self._send_observers):
            try:
                observer(request)
            except Exception:
                logger.debug("Send observer error", exc_info=True)

    async def connect(self) -> None:
        """Establish the WebSocket connection and start receiving."""
        logger.debug("Connecting to probe at %s", self._ws_url)
        self._ws = await connect(
            self._ws_url,
            ping_interval=10,   # send ping every 10s to keep connection alive
            ping_timeout=30,    # allow 30s for pong response
            close_timeout=5,    # 5s grace period on close
        )
        self._connected = True
        self._recv_task = asyncio.create_task(self._recv_loop())
        self._notification_task = asyncio.create_task(self._notification_dispatcher())
        logger.debug("Connected to probe at %s", self._ws_url)

    async def handshake(self) -> dict:
        """Fetch the probe's version and check wire-protocol compatibility.

        Kept out of connect() on purpose: connect() is what the test suite drives
        against a mock socket, and making it depend on a round-trip would couple
        transport setup to protocol semantics. The session layer calls this once
        the connection is up.

        A mismatch is reported as a warning rather than an error. The probe is
        already running inside someone's application -- refusing to talk to it
        strands the user with no way to inspect what they have, which is worse
        than degraded service.

        Returns:
            The raw getVersion result. Empty dict if the probe does not answer.
        """
        try:
            info = await self.call("getVersion")
        except (ProbeError, ConnectionError) as exc:
            logger.warning("Version handshake failed: %s", exc)
            return {}

        self._probe_version = info.get("version")
        raw_protocol = info.get("protocolVersion")
        self._probe_protocol_version = raw_protocol if isinstance(raw_protocol, int) else None

        if self._probe_protocol_version is None:
            # Every probe before this field existed reported a hardcoded "0.1.0"
            # and no protocol version at all, so absence is itself the signal.
            logger.warning(
                "Probe at %s reports no protocolVersion (version=%s). It predates "
                "protocol negotiation; some calls may fail in ways this client "
                "cannot explain.",
                self._ws_url, self._probe_version,
            )
        elif self._probe_protocol_version != SUPPORTED_PROTOCOL_VERSION:
            logger.warning(
                "Probe protocol version %d does not match this client's %d "
                "(probe build %s). Rebuild the probe and the qtpilot package from "
                "the same source revision.",
                self._probe_protocol_version, SUPPORTED_PROTOCOL_VERSION,
                self._probe_version,
            )
        else:
            logger.debug(
                "Handshake OK: probe %s, protocol v%d",
                self._probe_version, self._probe_protocol_version,
            )

        return info

    async def disconnect(self) -> None:
        """Close the connection and cancel all pending requests."""
        logger.debug("Disconnecting from probe")
        self._connected = False

        if self._recv_task is not None:
            self._recv_task.cancel()
            try:
                await self._recv_task
            except asyncio.CancelledError:
                pass
            self._recv_task = None

        if self._notification_task is not None:
            self._notification_task.cancel()
            try:
                await self._notification_task
            except asyncio.CancelledError:
                pass
            self._notification_task = None

        if self._ws is not None:
            await self._ws.close()
            self._ws = None

        # Resolve all pending futures with ConnectionError
        for future in self._pending.values():
            if not future.done():
                future.set_exception(ConnectionError("Disconnected from probe"))
        self._pending.clear()

    async def call(
        self,
        method: str,
        params: dict | None = None,
        timeout: float | None = _UNSET,
    ) -> dict:
        """Send a JSON-RPC 2.0 request and wait for the response.

        Args:
            method: The JSON-RPC method name.
            params: Optional parameters dict.
            timeout: Seconds to wait for the response. Omit for the default
                (DEFAULT_CALL_TIMEOUT, or QTPILOT_CALL_TIMEOUT); pass None to
                wait indefinitely.

        Returns:
            The result dict from the response.

        Raises:
            ProbeTimeoutError: If the probe does not respond within the deadline.
            ProbeError: If not connected or if the probe returns an error.
        """
        if timeout is _UNSET:
            timeout = _default_call_timeout()
        if not self._connected or self._ws is None:
            raise ProbeError("Not connected to probe")

        request_id = self._next_id
        self._next_id += 1

        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": request_id,
        }

        loop = asyncio.get_running_loop()
        future: asyncio.Future = loop.create_future()
        self._pending[request_id] = future

        t0 = time.monotonic()
        try:
            await self._ws.send(json.dumps(request))
            logger.debug("Sent request id=%d method=%s", request_id, method)
            self._notify_send_observers(request)
            if timeout is None:
                result = await future
            else:
                # shield() is deliberately NOT used: on timeout the request is
                # abandoned, so the future must be dropped from _pending too or
                # a late response resolves a future nobody is awaiting and the
                # dict grows for the life of the session.
                result = await asyncio.wait_for(future, timeout)
        except asyncio.TimeoutError as exc:
            self._pending.pop(request_id, None)
            duration_ms = (time.monotonic() - t0) * 1000
            timeout_error = ProbeTimeoutError(method, timeout)
            self._notify_call_observers(request, timeout_error, duration_ms)
            raise timeout_error from exc
        except asyncio.CancelledError:
            self._pending.pop(request_id, None)
            raise
        except Exception as exc:
            duration_ms = (time.monotonic() - t0) * 1000
            self._notify_call_observers(request, exc, duration_ms)
            raise
        else:
            duration_ms = (time.monotonic() - t0) * 1000
            self._notify_call_observers(request, result, duration_ms)
            return result

    async def _recv_loop(self) -> None:
        """Background task that reads WebSocket messages and resolves futures."""
        try:
            async for raw in self._ws:
                try:
                    msg = json.loads(raw)
                except (json.JSONDecodeError, TypeError):
                    logger.debug("Ignoring non-JSON message")
                    continue

                msg_id = msg.get("id")
                if msg_id is None or msg_id not in self._pending:
                    # JSON-RPC notification (no id, has method)
                    method = msg.get("method")
                    if method:
                        try:
                            self._notification_queue.put_nowait(
                                (method, msg.get("params", {}))
                            )
                        except asyncio.QueueFull:
                            self._notification_drops += 1
                    elif not method:
                        logger.debug("Ignoring message with id=%s", msg_id)
                    continue

                future = self._pending.pop(msg_id)
                if future.done():
                    continue

                if "error" in msg:
                    future.set_exception(ProbeError.from_jsonrpc(msg["error"]))
                else:
                    future.set_result(msg.get("result", {}))

        except asyncio.CancelledError:
            raise
        except Exception as exc:
            # Log close code/reason if available (websockets.ConnectionClosed)
            close_code = getattr(getattr(exc, "rcvd", None), "code", None)
            close_reason = getattr(getattr(exc, "rcvd", None), "reason", None)
            logger.warning(
                "WebSocket connection closed: %s (code=%s reason=%s)",
                exc, close_code, close_reason,
            )
        finally:
            self._connected = False
            # Cancel all remaining pending futures
            for future in self._pending.values():
                if not future.done():
                    future.set_exception(ConnectionError("WebSocket closed"))
            self._pending.clear()

    async def _notification_dispatcher(self) -> None:
        """Background task that dispatches notifications from queue to handlers."""
        try:
            while True:
                method, params = await self._notification_queue.get()
                for handler in list(self._notification_handlers):
                    try:
                        handler(method, params)
                    except Exception:
                        logger.debug("Notification handler error", exc_info=True)
        except asyncio.CancelledError:
            pass

    @property
    def notification_drops(self) -> int:
        """Number of notifications dropped due to full queue."""
        return self._notification_drops

    @property
    def notification_queue_size(self) -> int:
        """Current number of notifications waiting to be dispatched."""
        return self._notification_queue.qsize()
