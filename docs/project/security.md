<title>Security</title>

# Security

plcopen is an embeddable motion-control kernel and IEC 61131-3 ST runtime. It
is not a security sandbox, safety PLC, network service, or functionally
certified product.

## Supported scope

- `main` and the latest published release receive best-effort security fixes.
- Older releases are not guaranteed to receive backports.
- The project has no LTS line, response SLA, or paid security support.

## Reporting a vulnerability

Do not publish exploits, proof-of-concept details, credentials, real-device
data, or sensitive logs in a public issue, discussion, or pull request.

Until GitHub Private Vulnerability Reporting is available for the repository,
open a public issue titled `[Security contact request]` containing only the
affected component and a coarse impact category. Continue privately after the
maintainer provides a channel.

The exact current procedure and trust boundaries are maintained in the
[canonical security policy](https://github.com/lusipad/plcopen/blob/main/SECURITY.md).

## Safety boundary

Software security, real-time timing, and mechanical/electrical safety are
different assurance domains. Passing parser tests, WCET gates, or motion
simulation does not establish functional-safety certification or safe machine
integration.
