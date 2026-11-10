<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Filtering loaded results

The chips above the result tree control **display only**. They show the provider
facets present in this tab, for example Anime, TV, Games, General and USD pricing.
Use Tab then Space to toggle a chip, or click it. The horizontal chip strip can
scroll on narrow windows. Show all resets the selection.

Selected values within one facet match **any** value; different selected facets
must **all** match. Anime + Games shows either topic; adding USD pricing requires
that currency too. Sources without classification appear under Unclassified;
selecting it also includes those groups. Unknown supported facets use their
plain label/code fallback. Story-level MangaBaka tags are not provider facets.

Toggling chips never searches, requests details, changes provider enablement or
clears results. It only hides/shows existing tree groups. Cached details,
selection, paging tokens and in-flight operations remain owned by the same tab.
Results arriving while their group is hidden remain cached and available when
the filter is reset. Newly arriving groups follow the active filter. A hidden
selection's already loaded detail stays visible so context is not destroyed.

Source eligibility is a separate control in Settings/per-search selection. Use
an explicit Search/Refresh to fetch newly selected sources; these chips cannot
fetch providers that were never queried.
