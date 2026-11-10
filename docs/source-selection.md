<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Source selection and search eligibility

Settings → Source selection controls **which sources are queried**, separately
from chips that filter already loaded results. Group checkboxes are tri-state:
checked enables the group, unchecked disables it, partial inherits. Existing
Providers and Script packages checkboxes make explicit per-source overrides.
The effective-source list explains each source's state, including conflicts and
default-off reasons. Select a source and choose Use group / provider default to
remove its explicit override. Approval and credentials remain separate gates.

Persistent precedence is explicit provider override, then group policy, then
provider default. If a provider has multiple group policies, any disabled group
wins over enabled groups; an explicit provider override wins over that conflict.
Existing `_Enabled` values and their legacy migration remain unchanged. Inherited
choices stay absent; unrelated Settings Apply does not create provider overrides.
Policies live in the KConfig ProviderGroups group under stable facet IDs.

Each tab has Sources for this search and an explicit Search selected sources
action. Changing a checkmark or a Settings control initiates **zero requests**.
The action adds never-queried sources and resumes cancelled initial operations;
already loaded results/details and continuation state remain available. Excluded
sources stop pending work and future page/details fetches only when that action
is pressed. The result display filter is independent.

A deliberate temporary opt-in may override a group/default exclusion, but never
an explicit provider-off choice, missing approval, invalid package or credential
requirements. Use Settings to enable/reset a source explicitly disabled there.
Temporary selections are not written to configuration or copied to new tabs.
Default-off approved script packages are instantiated only on explicit search;
approval or opening the menu does not execute their entry points. Current
approval and explicit-off preferences are rechecked when applying a stale tab's
selection. USD pricing never implies a geographic restriction; no location
inference occurs.

The Settings policy page is independent of the resizable existing detail panes
in #137. Merge the defaults and metadata foundations first, then reconcile this
change with the Settings panes and true pop-out changes, preserving tab controls
when a session moves between windows.
