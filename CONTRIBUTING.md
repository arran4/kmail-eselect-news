<!--
SPDX-FileCopyrightText: 2026 Arran Ubels <arran4@gmail.com>
SPDX-License-Identifier: CC-BY-SA-4.0 OR LGPL-2.1-or-later
-->

# Development and integration testing

See the [README](README.md) for installation and daily use.

## Architecture

`NewsResource : Akonadi::ResourceBase` implements collection discovery, item synchronization, and batch payload retrieval. `NewsSource` and `loadNews` implement the independent filesystem backend; `NewsArticle::message` converts each record to a KMime message. The resource is its own process registered via a desktop metadata file.

References: [GLEP 42](https://www.gentoo.org/glep/glep-0042.html), [Akonadi ResourceBase](https://github.com/KDE/akonadi/blob/master/src/agentbase/resourcebase.h), [KDE Maildir resource](https://github.com/KDE/kdepim-runtime/tree/master/resources/maildir).

## Building from source

For development or packaging where a prebuilt package is unavailable. Gentoo users should normally use the [ebuild](README.md#installation).

### Requirements

- A C++20 compiler, CMake 3.22 or newer, and a build tool.
- KDE Extra CMake Modules (ECM), available as `kde-frameworks/extra-cmake-modules` on Gentoo.
- Qt 6 development libraries (Core, and Test for automated tests).
- KDE PIM 6 Akonadi development libraries and KMime (either KF6Mime or KPim6Mime CMake package, matching your installed stack).
- A running KDE PIM 6 session with Akonadi MIME support and KMail for live testing.
- A local Gentoo repository (Portage news state is optional), or an isolated test fixture.

Install dependencies through your distribution's package manager. No particular checkout path, CMake generator, or release tag is required.

### Configure, build and test

From the source directory:

```sh
cmake -S . -B build -DBUILD_TESTING=ON &&
cmake --build build &&
ctest --test-dir build --output-on-failure
```

Tests use temporary directories and do not touch system news state.

### Manual installation and removal

Only use manual installation if you are not managing the resource with a package manager. Configure the installation prefix to match the running Akonadi installation and ensure it can discover the executable and agent metadata. For example, on a system using /usr:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr &&
cmake --build build &&
DESTDIR="$PWD/build/stage" cmake --install build
```

Review the staged files. If there is no package-managed installation and you want to install system-wide, run `sudo cmake --install build`. Never run the resource or Akonadi as root. Follow the [installed-resource smoke test](#smoke-test).

Before removing a manual installation, remove the resource instance from KMail. Review `build/install_manifest.txt` and remove only files from this manual installation, not package-managed files. Restart Akonadi if needed. Do not delete its database.

## Optional isolated integration testing

For developers and regression testing. Start with an installed resource; see [testing on Gentoo](README.md#troubleshooting). These steps are not required for everyday installation or use.

This fixture overrides source directories through a user config file. It applies to every instance of this experimental resource; use a disposable KDE user or remove the live resource first. Do not overwrite an existing configuration. Run in a shell as the normal user.

```sh
config="${XDG_CONFIG_HOME:-$HOME/.config}/kmail-eselect-news.ini"
if [ -e "$config" ]; then
    echo "Existing configuration: $config; stop and back it up manually."
else
    fixture=$(mktemp -d "${TMPDIR:-/tmp}/eselect-news-test.XXXXXX") &&
    mkdir -p "$fixture/news/2026-01-01-example" "$fixture/state" "$(dirname "$config")"
    cat > "$fixture/news/2026-01-01-example/2026-01-01-example.en.txt" <<'EOF'
Title: Example Gentoo notice
Author: Test Developer <dev@example.invalid>
Posted: 2026-01-01
Revision: 1
News-Item-Format: 2.0

This entire news article must be visible in KMail's message body.
EOF
    printf '%s\n' 2026-01-01-example > "$fixture/state/news-gentoo.unread"
    printf '[General]\nrepository=gentoo\nnewsDirectory=%s/news\nstateDirectory=%s/state\n' \
        "$fixture" "$fixture" > "$config"
    printf 'Fixture: %s\nConfiguration: %s\n' "$fixture" "$config"
fi
```

Keep this shell open for the remaining steps; do not continue if the config already existed. Add the resource using the [smoke test instructions](#smoke-test), synchronize and verify there is exactly one unread message. Refresh twice; verify one message remains. Open it and confirm the text is the body. Then:

```sh
# Simulate an external reader marking it read (fixture only).
mv "$fixture/state/news-gentoo.unread" "$fixture/state/news-gentoo.read"
# After refresh: same message, now read.
# Edit the notice, then refresh: same ID, updated body.
printf '\nA revised paragraph.\n' >> "$fixture/news/2026-01-01-example/2026-01-01-example.en.txt"
# Invalid state: refresh should keep the same article, still read.
printf '%s\n' ../invalid > "$fixture/state/news-gentoo.unread"
# Remove the invalid state and refresh to recover the read flag.
rm "$fixture/state/news-gentoo.unread"
# Simulate a retained skip marker after purging read state: article stays in the main folder, read.
printf '%s\n' 2026-01-01-example > "$fixture/state/news-gentoo.skip"
rm "$fixture/state/news-gentoo.read"
# Refresh, then remove skip state and refresh:
rm "$fixture/state/news-gentoo.skip"
# Article moves to Skipped, still read.
# Simulate Portage selecting it again:
printf '%s\n' 2026-01-01-example > "$fixture/state/news-gentoo.unread"
printf '%s\n' 2026-01-01-example > "$fixture/state/news-gentoo.skip"
# Refresh: article returns to the main repository folder, unread.
# Remove the fixture article itself; after refresh the folder remains, empty.
rm "$fixture/news/2026-01-01-example/2026-01-01-example.en.txt"
```

Remove the fixture resource through KMail's Receiving settings or Akonadi Console before cleaning up. Only after confirming this is the fixture you created:

```sh
rm -- "$config"
rm -r -- "$fixture"
```

Re-add the resource to return to real system news. Do not edit or delete actual /var/lib/gentoo/news files for this test.

## Smoke test

Run as your desktop user:

```sh
command -v akonadi_eselect_news_resource
akonadictl status
eselect news list
```

If the resource is not listed in KMail, close KDE PIM applications and run `akonadictl restart` as your desktop user. Open KMail and add **Gentoo News** under Settings → Configure KMail → Accounts → Receiving. Alternatively, add it through Akonadi Console's Agents tab.

Synchronize the account, then check:

- [ ] A child folder named after each configured repository appears beneath Gentoo News, even if an individual repository has no news.
- [ ] Available English Gentoo notices appear as ordinary email messages.
- [ ] Subjects match news titles, dates match publication dates, and the full article appears in the message body (not an attachment).
- [ ] Known read/unread state matches Portage when accessible; only explicitly unread articles not overridden by read state appear unread.
- [ ] Each repository has a Skipped child folder; notices absent from all three lists appear there as read; previously queued notices (including skip-only entries) and notices with uncertain state remain in the main folder.
- [ ] Repeated synchronization does not create duplicates.
- [ ] Opening a notice does not change Portage's read state.
- [ ] The folder does not advertise message creation or deletion.
- [ ] Restarting KMail/Akonadi preserves the collection and message identities.

An empty collection is valid if a repository has no news, but the collection itself should still appear. An account showing Ready with no collection is not proof of a successful sync. If it occurs, manually synchronize the resource from Akonadi Console's Agents tab and inspect Browser for its collection. The account should be named Gentoo News unless renamed by the user. Do not modify system news files just to test the resource.


## Upstream adoption and contributions

This is an independent project, not an official KDE or Gentoo component. Adoption, modification and redistribution are welcome under the stated licenses, without a separate permission request. Contributions should preserve relevant copyright notices and use the corresponding file's license; no copyright assignment is required by this project.

For KDE, follow the [KDE licensing policy](https://community.kde.org/Policies/Licensing_Policy) and kdepim-runtime conventions. For Gentoo, follow the [Gentoo development guide](https://devmanual.gentoo.org/). Before submission, verify the build and integration on Gentoo, review localization and KDE build conventions, and agree on maintenance and platform gating. Official Gentoo packaging should use publicly accessible release sources, a generated distfile Manifest and tested architecture keywords. The public arrans-overlay package uses release archives with a distfile Manifest.

Run `reuse lint` in the source root to check license metadata.

