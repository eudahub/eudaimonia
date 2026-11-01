<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Contributing to Eudaimonia

Welcome. Start with the [README](README.md); the rules for coding assistants in
[AGENTS.md](AGENTS.md) describe the project's conventions as well. By
contributing you accept the rules below.

## Pick a bounded result

Use an issue in [the project tracker](https://github.com/eudahub/eudaimonia/issues)
to coordinate work, or propose a task in a pull request if issues are
unavailable. Check for existing work before starting. Pick one observable
behavior, not a whole subsystem: one action of the IDE, one plugin interface,
one build tool.

## Prepare a change

1. Read only the context needed for the task, following links to authoritative
   specifications (CMake file API, Language Server Protocol, Debug Adapter
   Protocol) where necessary.
2. Keep one concern per pull request. Describe the behavior before and after
   the change; user-visible changes go into `README.md`.
3. Follow existing conventions and match the style of the surrounding code.
   Do not establish a plugin interface by accident: a new interface carries a
   versioned IID (`io.github.eudahub.eudaimonia.<Interface>/<n>`) and is
   proposed together with its first implementation.
4. Write code, comments, documentation and commit messages in English.
5. Make sure the project builds (`cmake --build build`) and test the relevant
   behavior, including error paths. State which tests you ran and which you
   did not.
6. Add a one-line SPDX identifier to new source files
   (`// SPDX-License-Identifier: GPL-3.0-only`). Preserve attribution and
   license notices of code taken from elsewhere, and record its origin.
7. Include a handoff note if unfinished: what works, what failed, the next
   command and unresolved decisions.

Do not commit build products, downloaded source trees, credentials, logs,
settings files, personal paths or private conversations.

## AI-assisted work

Use capable models for analysis, implementation, debugging, or a second
review when useful. Human contributors remain responsible for the submitted
change. Work without AI is equally welcome.

State briefly whether and how AI helped; naming a model is optional. Do not
submit entire chat histories. Confirm generated API names against the pinned
sources or specifications: a model's confidence is not a test result.

## License of contributions

Eudaimonia is licensed under the GNU General Public License, version 3 only
(GPL-3.0-only); see [LICENSE](LICENSE). Your contribution is licensed under
the same terms as the rest of Eudaimonia.

Plugins loaded by Eudaimonia form one work with it and must be under a
license compatible with the GPL-3.0 (for example GPL-3.0, LGPL-3.0,
Apache-2.0 or MIT).

## Later versions of the GPL

Section 14 of the GPL-3.0 allows a program to name a proxy who can decide
which future versions of the GNU General Public License may be used. For
Eudaimonia, the proxy is its author, Andrzej Borucki, or a person the author
names publicly as the proxy.

When a later version of the GPL (such as version 4) is published, the proxy
examines it and may accept it only when it suits the project; nothing makes
the acceptance automatic. An acceptance is a public statement, such as a note
in this repository; from then on, Eudaimonia, including your contribution,
may be used under that version as well. Until then, only version 3 applies.

By contributing, you agree that this proxy decision applies to your
contribution, so that Eudaimonia never needs the consent of every contributor
to move to a later version of the GPL.

## Review and acceptance

- A change does what it describes and the relevant tests pass.
- Plugin interfaces document ownership, threading, blocking behavior and
  errors.
- Dependencies have recorded origins and licenses compatible with the
  GPL-3.0.
- The author distinguishes tests actually run from suggested or skipped
  checks.
- A maintainer reviews the change.
