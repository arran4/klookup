# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Git expands these fields in archives; they are not maintained version values.
set(_KLOOKUP_GIT_ARCHIVE_DESCRIPTION "$Format:%(describe:tags,abbrev=12,match=v[0-9]*)$")
set(_KLOOKUP_GIT_ARCHIVE_REFS "$Format:%D$")
set(_KLOOKUP_GIT_ARCHIVE_REVISION "$Format:%H$")
