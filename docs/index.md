# KLookup developer documentation

KLookup is a KDE Plasma desktop lookup application built with C++17, Qt 6 and KDE Frameworks 6. These guides document current desktop behaviour and provider extension points.

For installation, source builds and contribution instructions, see the [repository README](https://github.com/arran4/klookup#readme) and [contributor guide](https://github.com/arran4/klookup/blob/main/CONTRIBUTING.md). Source and automated tests remain authoritative for implemented behaviour.

## Desktop behaviour

- [Autostart on login](autostart.md), [global shortcuts](global-shortcuts.md) and [clipboard privacy](clipboard-policy.md).
- [Recent activity](recent-activity.md) and [result interactions](result-interactions.md).
- [Typed result metadata](structured-metadata.md) for provider data.

## Provider development

- [Package installation and approval](provider-package-approval.md), [authentication](provider-authentication.md) and [diagnostics](provider-diagnostics.md).
- [Script lifecycle](script-provider-lifecycle.md), [HTTP transport](provider-http-transport.md) and [continuation contract](provider-continuation.md).
- Provider-specific guides: [Wikipedia](wikipedia-content.md), [MangaBaka](mangabaka-formats.md), [Spotify](spotify-provider.md), [AniList](provider-anilist.md), [Lyrics.ovh](provider-lyrics-ovh.md) and [Open Library](provider-openlibrary.md).

## Preview locally

Install the same documentation generator used in CI through the Python package manager:

```sh
python -m pip install 'mkdocs==1.6.1'
mkdocs serve
```

CI validates the site on pull requests and deploys from a fully validated `main` build only after the repository's GitHub Pages source is configured for **GitHub Actions**. The site is documentation, not a software release.
