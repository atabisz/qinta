# Security policy

## Reporting a vulnerability

Please report security issues privately through GitHub's
[private vulnerability reporting](https://github.com/atabisz/qinta/security/advisories/new),
not in a public issue.

Qinta opens Markdown files that may come from anywhere, so the issues I most want to hear about are
ones a crafted document can trigger: crashes or memory corruption in parsing, layout or export,
files read or written outside what the user chose, and network requests the user did not expect.

Include the document that triggers it if you can, along with the platform and the Qinta version or
commit.

## Supported versions

Only the latest commit on `master` is supported. There are no tagged releases yet.

## Upstream

Qinta is a fork of [Tinta](https://github.com/oipoistar/tinta). If the issue is in code Qinta shares
with Tinta, I'll pass it on upstream once it has been fixed here.
