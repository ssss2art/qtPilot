# Probe authentication and operating profiles

This branch implements opt-in admission controls without changing the existing
unconfigured LAN/injection contract. Authentication authorizes full probe access;
it does not provide per-user roles or identify a particular human.

## Protocol and threat boundary

Use an operator-provisioned random bearer token in the HTTP `Authorization`
header of the WebSocket upgrade. A configured probe checks it before assigning
the sole active client, connecting dispatch signals or creating subscriptions.
Rejected sockets close without calling application code. No JSON-RPC method is
an authentication bypass. Origin policy is checked independently, including for
clients holding the token.

After admission, an authenticated probe sends `qtpilot.authenticated` before
normal traffic. A client configured with a token waits at most five seconds for
that acknowledgement before exposing a connected session. A legacy probe that
ignores the header cannot silently satisfy authenticated mode. There is no
fallback to unauthenticated or plaintext access.

Bearer tokens require verified TLS for non-loopback endpoints. The probe supports
`wss://` using Qt's TLS backend with TLS 1.2 or later; the Python client verifies
certificate chain and hostname using the system trust store or a supplied CA.
Loopback-only authenticated `ws://` is permitted for same-machine development.
Use literal loopback IPs for this exception; a hostname resolving locally is not
proof of a trusted network boundary. Remote profiles require TLS even on loopback.
A missing TLS backend, invalid settings or unreadable credentials refuses probe
startup while the host application remains usable. TLS does not protect a
compromised host or another process able to read the credential file.

Qt controls upgrade parsing and TLS negotiation. The probe limits admission to
16 pending sockets, a five-second TCP/TLS/upgrade deadline and a 16 KiB upgrade
read buffer. These bounds are not a claim of Internet-facing denial-of-service
resistance. Admission failures do not consume the active client slot. Platform
validation must exercise the supported Qt versions and TLS backends.

## Configuration and lifetime

| Setting | Purpose |
| --- | --- |
| `QTPILOT_PROFILE` | Unset legacy behavior, or `local`, `trusted-network`, `remote` |
| `QTPILOT_AUTH_TOKEN_FILE` | Private file holding a random URL-safe token (32–256 ASCII characters, optional final newline) |
| `QTPILOT_TLS_CERT_FILE` | PEM certificate chain used by the probe |
| `QTPILOT_TLS_KEY_FILE` | Unencrypted PEM private key used by the probe |
| `QTPILOT_TLS_CA_FILE` | Optional PEM CA bundle trusted by the Python client |
| `QTPILOT_BIND_ADDRESS` | Existing explicit exposure selector; conflicts with a profile fail closed |

Credential file contents never belong in command-line arguments, URLs,
discovery, status, recordings or logs. Credentials are read at connection/probe
startup, retained only in the owning session and rotated by restarting that
session. Production credential provisioning is external; tests generate throwaway
credentials and certificates at runtime. The published Python package gains no
Qt dependency. Mobile static consumers use the same environment settings before
Qt startup and still must be restricted to development builds.

| Profile | Bind | Discovery | Requirements |
| --- | --- | --- | --- |
| Unset | Existing bind policy, LAN by default | Existing broadcast/loopback behavior | Explicit auth/TLS settings are enforced if supplied |
| `local` | Loopback | Disabled | Authentication optional; conflicting LAN bind refused |
| `trusted-network` | LAN | Enabled | Token and TLS certificate/key required |
| `remote` | Loopback by default; explicit LAN bind allowed | Disabled | Token and TLS certificate/key required |

CLI settings override the same environment setting. A conflicting explicit bind
and profile is an error; invalid profiles never fall back to legacy LAN. The
probe resolves one effective configuration for listening and discovery rather
than re-reading policy independently after the server has started. Discovery is
an untrusted hint and may advertise TLS/auth requirements, never secrets; the
client's configured requirements cannot be weakened by an announcement.

## Launch examples

For one-machine development, with no UDP discovery:

```sh
build/bin/qtPilot-launcher --profile local /path/to/synthetic-app
qtpilot serve --profile local --ws-url ws://127.0.0.1:9222
```

For an authenticated LAN probe, provision a random token file and a certificate
whose subject alternative name matches the intended IP or hostname. Keep the
private key on the target host; distribute only the token and trusted CA to the
controller through your existing secure provisioning channel. Restrict credential
files to the operator account. PEM RSA and EC private keys are supported.

```sh
# Target host
build/bin/qtPilot-launcher --profile trusted-network \
  --auth-token-file /private/probe.token \
  --tls-cert-file /private/probe-chain.pem --tls-key-file /private/probe-key.pem \
  /path/to/synthetic-app

# Controller host; the certificate must cover this example hostname
qtpilot serve --profile trusted-network --ws-url wss://probe.example.invalid:9222 \
  --auth-token-file /private/probe.token --tls-ca-file /private/probe-ca.pem
```

`serve` and `demo` accept the certificate/key options when launching a target.
`serve`, `demo` and `replay` accept profile, token-file and CA-file options. The
native launcher accepts profile, token-file and certificate/key-file options.
The same environment variables work for injected and development-only static
consumers. No raw token argument exists. Omitted CLI settings preserve the
environment; an explicitly empty credential path is invalid.

`remote` binds loopback unless `QTPILOT_BIND_ADDRESS=any` is explicitly supplied.
It requires TLS and the token even through a forwarded port, and suppresses UDP
on both probe and controller. Use a URL matching the certificate through your
tunnel. Client redirects and implicit proxy routing are disabled. Changing a
certificate or token file requires restarting the probe/client session.

## Separate-host acceptance

The local suite proves native admission, certificate validation and UDP behavior
on one machine. Before claiming a completed LAN acceptance gate, use two real
hosts on the intended network and retain sanitized evidence under ignored `logs/`:

1. Start two synthetic target instances on host A, using `trusted-network` and
   distinct ports. Start the controller on host B with the matching token and CA.
2. Read `qtpilot_status`: both address/PID/port identities must appear independently
   with `wss://` URLs. Announcements are hints; select the intended endpoint and
   verify `qt_ping` identifies the expected PID before driving it.
3. Connect to each instance in turn; use an object property write/read to verify
   effects through the wire. A second controller must not displace the owner.
4. Repeat with a wrong token, wrong CA and wrong certificate hostname. None may
   expose a connected session or drive a property change. Restore credentials and
   verify a successful connection without restarting a healthy probe.
5. Restart one target and verify its new PID is distinct; disconnect/reconnect,
   then verify stale discovery expiry for the departed instance.
6. Repeat the discovery check with `local` and `remote`: neither announces. With
   profile and credentials unset, the existing LAN discovery/control contract
   remains enabled. Use only an isolated trusted development network for that
   legacy unauthenticated check.

Record the two OS/Qt/runtime versions, revisions, selected endpoint shapes,
observed results and exit codes. Remove host/application identifiers and secrets
before selecting any tracked evidence. This separate-host gate is **pending**;
same-host tests do not establish it. Mobile builds likewise establish compilation
and static startup, not on-device TLS backend availability.

## Acceptance evidence

Each slice must prove behavioral red then green: configuration conflicts;
wrong/missing token with zero sentinel effects; stalled upgrades alongside a valid
client; Origin rejection with valid credentials; successful verified TLS; wrong
CA/hostname and plaintext downgrade refusal; redacted debug/recording output;
disconnect/reconnect and unconfigured legacy LAN. Compile and run the supported
Qt floor/current desktop matrix and retain mobile/static compile gates before
claiming support there.

API references: [Qt WebSocket request headers](https://doc.qt.io/qt-6/qwebsocket.html#request),
[Qt WebSocket TLS and admission configuration](https://doc.qt.io/qt-6/qwebsocketserver.html),
[Qt 5.15 server API](https://github.com/qt/qtwebsockets/blob/5.15/src/websockets/qwebsocketserver.h),
and [Python websockets connection options](https://websockets.readthedocs.io/en/stable/reference/asyncio/client.html).
