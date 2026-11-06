<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Provider display preferences

This foundation is for provider-specific **presentation** options, not source
classification, source eligibility or the legacy server-side format controls.
Providers may declare up to 32 version-1.0.0 displayOptions descriptors in their
manifest or native `setDisplayOptions()` declaration. `displayOptions()` is a
nonvirtual read-only getter over QObject metadata, preserving native-provider
virtual slots; `cloneForSearch()` carries it to new search instances. Both API
adapters preserve it;
passive manifest discovery never executes provider code.

Each descriptor has a stable namespaced key, label/localized labels, type,
validated default and optional global inheritance. Types are boolean, enum,
ordered-list of declared IDs and bounded integer range. Choices, translations,
key/text lengths and ranges are bounded. Arbitrary text, credential fields and
executable expressions are unsupported. All UI labels are plain text.

Precedence: temporary current-result view > saved provider default > applicable
global default > descriptor default. A Context lives with its result view;
opening another tab starts with inherited values. Removing temporary choices
never erases saved values. Settings use separate versioned DisplayProvider and
DisplayGlobal KConfig namespaces; incompatible saved values fall back without
silently overwriting the original entry. Provider IDs are encoded, not file paths.

Results from providers declaring these options expose View display options in
the context menu. Controls report inherited/overridden state. Save provider
default and Save global default are explicit promotions; Reset can clear the
view or either saved scope. Undo restores the last saved-default change only
if another view has not subsequently changed that saved entry. Global promotion
never deletes a provider override; it keeps the current view choice temporary
when that is necessary to preserve the selected value.

Changing controls does not dispatch provider work. requiresRefresh descriptors
show an explicit notice; consumers must request any refetch through the ordinary
Search/Refresh flow, never from an option toggle. The host emits effective-value
changes for result rendering. Existing providers advertise no new options until
their consumer implements those semantics; this foundation does not pretend to
filter MangaBaka tags already. #135 will declare distinct weight, spoiler,
implied and content-level options; #134/#146 can use shared options where their
semantics actually match. It does not invent a cross-provider score scale.

Manifest shape example:

```json
"displayOptions": [{
  "version": "1.0.0", "key": "org.example.presentation.show-spoilers",
  "label": "Show spoilers", "type": "boolean", "default": false,
  "global": false, "requiresRefresh": false
}]
```
