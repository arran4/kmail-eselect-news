# Optional isolated integration testing

For developers and regression testing. Start with an installed resource; see [testing on Gentoo](testing-gentoo.md). These steps are not required for everyday installation or use.

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

Keep this shell open for the remaining steps; do not continue if the config already existed. Add the resource using the [smoke test instructions](testing-gentoo.md), synchronize and verify there is exactly one unread message. Refresh twice; verify one message remains. Open it and confirm the text is the body. Then:

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

