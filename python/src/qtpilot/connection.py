"""WebSocket + JSON-RPC client for communicating with the qtPilot probe."""

from __future__ import annotations

import asyncio
import json
import logging
import time
import warnings
from collections.abc import Callable

from websockets.asyncio.client import connect

logger = logging.getLogger(__name__)


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

    @property
    def is_connected(self) -> bool:
        """Whether the WebSocket connection is currently active."""
        return self._connected

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
        logger.debug("Connected to probe at %s", self._ws_url)

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

        if self._ws is not None:
            await self._ws.close()
            self._ws = None

        # Resolve all pending futures with ConnectionError
        for future in self._pending.values():
            if not future.done():
                future.set_exception(ConnectionError("Disconnected from probe"))
        self._pending.clear()

    async def call(self, method: str, params: dict | None = None) -> dict:
        """Send a JSON-RPC 2.0 request and wait for the response.

        Args:
            method: The JSON-RPC method name.
            params: Optional parameters dict.

        Returns:
            The result dict from the response.

        Raises:
            ProbeError: If not connected or if the probe returns an error.
        """
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
            result = await future
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
                    if method and self._notification_handlers:
                        for handler in list(self._notification_handlers):
                            try:
                                handler(method, msg.get("params", {}))
                            except Exception:
                                logger.debug(
                                    "Notification handler error", exc_info=True
                                )
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
        except Exception:
            logger.debug("WebSocket connection closed", exc_info=True)
        finally:
            self._connected = False
            # Cancel all remaining pending futures
            for future in self._pending.values():
                if not future.done():
                    future.set_exception(ConnectionError("WebSocket closed"))
            self._pending.clear()
