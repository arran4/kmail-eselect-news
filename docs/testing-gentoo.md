# Build and test on Gentoo (Qt 6 / KDE PIM 6)

Run as your normal desktop user except for the explicit emerge/install steps. Do not run KMail, Akonadi, or the resource as root.

## 1. Dependencies and build

On a working KMail installation, most dependencies should already be present. On current Gentoo, KMime is in kde-frameworks; older KDE Gear packages use kde-apps/kmime instead. Use the one matching your installed KDE PIM stack (do not force a package transition solely for this sample).

```sh
sudo emerge --ask --oneshot dev-build/cmake dev-build/ninja dev-qt/qtbase:6 \
    kde-apps/akonadi kde-apps/akonadi-mime kde-frameworks/kmime
# For older Gear: substitute kde-apps/kmime for kde-frameworks/kmime.
git clone https://github.com/arran4/kmail-eselect-news.git
cd kmail-eselect-news
git switch sample/akonadi-eselect-news  # omit after merging to main
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON &&
cmake --build build &&
ctest --test-dir build --output-on-failure
```

The unit tests create temporary directories (no Portage database changes) and check relevant-news filtering, duplicates, UTF-8 subject, original MIME body, stable identity, read-state revision changes, malformed IDs, missing articles and empty state.

## 2. Install and register

Close KMail and other KDE PIM apps before restarting Akonadi. The restart temporarily interrupts all accounts, but does not clear the database. Save unsent drafts first.

```sh
sudo cmake --install build
command -v akonadi_eselect_news_resource
test -f /usr/share/akonadi/agents/eselectnewsresource.desktop
akonadictl restart
```

Alternatively, inspect the install with `DESTDIR="$PWD/build/stage" cmake --install build` before installing system-wide. This is a manual install, not tracked by Portage.

Open KMail, go to Settings → Configure KMail → Accounts → Receiving → Add, and select **Gentoo News**. Alternatively create the resource with Akonadi Console's Agents tab (package kde-apps/akonadiconsole if needed). There is no account setup dialog. Synchronize the account manually using the folder/account context menu or the Agents tab.

## 3. Test against a real Gentoo repository

```sh
portageq get_repo_path / gentoo
eselect news list
ls -l /var/lib/gentoo/news/
```

Compare the visible notices with the read/unread lists and `eselect news list`. If no relevant articles exist, use the fixture below. Verify:

- A relevant notice appears with its original title and a publication date.
- Open the message: the whole original text appears in the body, not as an attachment.
- Read notices and unread notices initially match Portage's state. Opening mail does not change eselect state.
- Synchronize repeatedly; no duplicate items are created. After restarting Akonadi/KMail, the collection and item identity remain stable.
- Create/delete/move operations are not advertised. No source file is altered.

## 4. Deterministic integration fixture

This fixture overrides source directories through a user config file. It applies to every instance of this experimental resource; use a disposable KDE user or remove the live resource first. Do not overwrite an existing configuration. Run in a shell as the normal user.

```sh
fixture=$(mktemp -d "${TMPDIR:-/tmp}/eselect-news-test.XXXXXX")
config="${XDG_CONFIG_HOME:-$HOME/.config}/kmail-eselect-news.ini"
if [ -e "$config" ]; then
    echo "Existing configuration: $config; stop and back it up manually."
else
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

Keep this shell open for the remaining steps; do not continue if the config already existed. Start/add the resource as above, synchronize and verify there is exactly one unread message. Refresh twice; verify one message remains. Open it and confirm the text is the body. Then:

```sh
# Simulate an external reader marking it read (fixture only).
mv "$fixture/state/news-gentoo.unread" "$fixture/state/news-gentoo.read"
# After refresh: same message, now read.
# Edit the notice, then refresh: same ID, updated body.
printf '\nA revised paragraph.\n' >> "$fixture/news/2026-01-01-example/2026-01-01-example.en.txt"
# Temporarily reference a missing article: refresh should fail without wiping the cache.
printf '%s\n' 2026-01-02-missing > "$fixture/state/news-gentoo.unread"
# Refresh and check the error in Akonadi Console; remove the broken reference.
rm "$fixture/state/news-gentoo.unread"
# Refresh to recover. Then simulate purge:
rm "$fixture/state/news-gentoo.read"
# Refresh: the collection should now be empty.
```

Remove the fixture resource through KMail's Receiving settings or Akonadi Console before cleaning up. Only after confirming this is the fixture you created:

```sh
rm -- "$config"
rm -r -- "$fixture"
```

Re-add the resource to return to real system news. Do not edit or delete actual /var/lib/gentoo/news files for this test.

## 5. Diagnostics and uninstall

```sh
akonadictl status
command -v akonadi_eselect_news_resource
ldd /usr/bin/akonadi_eselect_news_resource
journalctl --user -b --no-pager | grep -Ei 'akonadi|eselect.news' | tail -n 100
```

In Akonadi Console, inspect Agents (status/error and synchronize), Browser (collection rights, item remote ID, flags, payload) and Debugger. Check that the installed executable and desktop file have the same resource identifier and that Qt/KDE libraries match your running Akonadi version.

Remove the resource instance from KMail first, close PIM applications, then remove only the two installed files:

```sh
sudo rm -- /usr/bin/akonadi_eselect_news_resource /usr/share/akonadi/agents/eselectnewsresource.desktop
akonadictl restart
```

Do not reset Akonadi or delete its database. If using a non-default installation prefix, use the matching paths in build/install_manifest.txt.

## Validation status

The API was reviewed against upstream KDE headers and resource examples. Compilation, CTest and live KMail integration require a Gentoo or equivalent KDE PIM 6 environment and have not yet been performed for this sample.
