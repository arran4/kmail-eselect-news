# Testing the installed resource on Gentoo

This guide assumes the resource is installed and you have a working KMail/Akonadi session. See the [installation instructions](../README.md#installation) if needed. No source checkout or manual installation is required.

## Smoke test

Run as your desktop user:

```sh
command -v akonadi_eselect_news_resource
akonadictl status
eselect news list
```

If the resource is not listed in KMail, close KDE PIM applications and run `akonadictl restart` as your desktop user. Open KMail and add **Gentoo News** under Settings → Configure KMail → Accounts → Receiving. Alternatively, add it through Akonadi Console's Agents tab.

Synchronize the account, then check:

- [ ] Relevant Gentoo notices appear as ordinary email messages.
- [ ] Subjects match news titles, dates match publication dates, and the full article appears in the message body (not an attachment).
- [ ] Initial read/unread state matches Portage.
- [ ] Repeated synchronization does not create duplicates.
- [ ] Opening a notice does not change Portage's read state.
- [ ] The folder does not advertise message creation or deletion.
- [ ] Restarting KMail/Akonadi preserves the collection and message identities.

An empty collection is valid if there are no relevant news items. Do not modify system news files just to test the resource.

## Optional regression testing

To exercise updates, purges and error recovery even when no real notices are available, use the [isolated integration fixture](testing-fixtures.md). Developers can run the automated tests as described in [building from source](building.md).

## Troubleshooting

```sh
akonadictl status
portageq get_repo_path / gentoo
ls -l /var/lib/gentoo/news/
journalctl --user -b --no-pager | grep -Ei 'akonadi|eselect.news' | tail -n 100
```

Check resource status, synchronize and inspect collection/item metadata in Akonadi Console. Confirm your desktop user can read the news and state directories. Do not run KMail or Akonadi as root or reset the Akonadi database.

## Uninstall

Remove the Gentoo News account from KMail or Akonadi Console, then let Portage remove the installed files:

```sh
sudo emerge --ask --depclean kde-apps/kmail-eselect-news
```

Close KDE PIM applications and restart Akonadi if necessary. For a manually installed development build, follow its [separate uninstall instructions](building.md#manual-installation-and-removal).

## Validation status

The smoke test and integration checks are expected behaviour, not recorded successful results. Build and live integration have not yet been verified.
