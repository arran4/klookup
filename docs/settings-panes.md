<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Settings lists and details

Providers, Script packages and Provider health each have a vertical separator
between the list and scrollable details. Drag it or focus it with Tab and use
Up/Down; Enter, Space or a double-click resets equal proportions. Lists and
details retain usable minimum heights at small sizes and high DPI. Authentication
and package actions remain inside the details pane, with dialog Apply/OK/Cancel
outside the separator.

Each page saves its proportions independently in KConfig's Layout group when
its separator is deliberately moved. Closing or resizing an older dialog never
overwrites another dialog's preference. Selection, approval drafts and diagnostic
operations retain their existing state; resizing does not start provider checks
or search requests. Detailed identity, origins, fingerprint and health output
remain scrollable. Credential behavior is unchanged.
