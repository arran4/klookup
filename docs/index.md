# KLookup developer documentation

KLookup is a KDE Plasma desktop lookup application built with C++17, Qt 6 and KDE Frameworks 6. These guides cover supported desktop behaviour and provider extension points.

To build, install or contribute, start with the [repository README](https://github.com/arran4/klookup#readme) and [contributor guide](https://github.com/arran4/klookup/blob/main/CONTRIBUTING.md). The source and automated tests remain authoritative for the current implementation.

## Desktop behaviour

- [Autostart on login](autostart.md), [global shortcuts](global-shortcuts.md) and [clipboard policy](clipboard-policy.md).
- [Recent activity](recent-activity.md) and [result interactions](result-interactions.md).
- [Typed result metadata](structured-metadata.md) for consistent provider results.

## Developing providers

- [Package installation and approval](provider-package-approval.md), [authentication](provider-authentication.md) and [provider diagnostics](provider-diagnostics.md).
- [Script operation lifecycle](script-provider-lifecycle.md), [HTTP transport](provider-http-transport.md) and [continuation contract](provider-continuation.md).
- Provider-specific guides: [Wikipedia](wikipedia-content.md), [MangaBaka](mangabaka-formats.md), [Spotify](spotify-provider.md), [AniList](provider-anilist.md), [Lyrics.ovh](provider-lyrics-ovh.md), [Open Library](provider-openlibrary.md) and [CheapShark](provider-cheapshark.md).

## Preview locally

Use the Python package manager to install the same MkDocs version used in CI:

```sh
python -m pip install 'mkdocs==1.6.1'
mkdocs serve
```

The [CI workflow](https://github.com/arran4/klookup/blob/main/.github/workflows/ci.yml) validates this documentation on pull requests and deploys it from passing builds on `main` when repository Pages is configured for **GitHub Actions**. Pages contains documentation only; it does not publish a KLookup software release.
