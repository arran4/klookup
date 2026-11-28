# KLookup developer documentation

KLookup is a KDE Plasma desktop lookup application built with C++17, Qt 6 and KDE Frameworks 6. These guides document current desktop behaviour and provider extension points.

For installation, source builds and contribution instructions, see the [repository README](https://github.com/arran4/klookup#readme) and [contributor guide](https://github.com/arran4/klookup/blob/main/CONTRIBUTING.md). Source and automated tests remain authoritative for implemented behaviour.

## Desktop behaviour

- [Autostart on login](autostart.md), [global shortcuts](global-shortcuts.md) and [clipboard privacy](clipboard-policy.md).
- [Recent activity](recent-activity.md), [preferred title languages](language-preferences.md) and [result interactions](result-interactions.md).
- [Typed result metadata](structured-metadata.md) and [source selection & result filtering](source-selection.md).

## Provider development

- [Shipped ECMAScript providers and Provider API v2](script-providers.md)
- [Package installation and approval](provider-package-approval.md), [classification](provider-classification.md), [authentication](provider-authentication.md) and [diagnostics](provider-diagnostics.md).
- [Script lifecycle](script-provider-lifecycle.md), [HTTP transport](provider-http-transport.md) and [continuation contract](provider-continuation.md).
- Native provider guides: [Wikipedia](wikipedia-content.md), [MangaBaka formats](mangabaka-formats.md) and [Spotify](spotify-provider.md).

## Preview locally

```sh
python -m pip install 'mkdocs==1.6.1'
mkdocs serve
```

CI validates the site on pull requests and deploys from a validated `main` build.
