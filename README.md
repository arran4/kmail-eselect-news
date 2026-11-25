# kmail-eselect-news

An experimental native Akonadi resource for Gentoo GLEP 42 news, shown in KMail as ordinary plain-text emails. No IMAP, SMTP, maildir export, root service or network access is involved.

## Behaviour and boundaries

- One read-only collection, **Gentoo News (gentoo)**, for the selected Portage repository.
- Uses `portageq get_repo_path / gentoo` to find the local repository; reads English news under `metadata/news`.
- Only IDs in Portage's `news-<repository>.read` and `.unread` files are included, matching the relevant-news view rather than every published notice. Duplicates are collapsed; unread wins if both lists contain an ID.
- Title becomes the subject; publication date becomes the message date (midnight UTC); the entire original news text, including its metadata and authors, becomes the UTF-8 plain-text body. Synthetic sender/recipient and stable Message-ID make clear these are locally generated messages.
- Stable Akonadi remote ID is repository + news ID; revision hashes content and source read state. Edits update the same item. A successful full snapshot removes notices no longer in either list (e.g. after purge).
- The source is **never written**. KMail reading a message does not acknowledge it to Portage. Use eselect separately; the next sync reflects the source state. KMail may restrict local flag changes on a read-only folder.
- Missing or malformed referenced articles fail the snapshot rather than silently deleting existing cached mail. A missing state file is treated as empty; an inaccessible source/state directory is an error.
- Initially one repository, English only, no GUI configuration, no backend search or outgoing transport. No privilege elevation. Periodic polling requested via Akonadi cache policy (15 minutes); manual refresh is supported.
- This is an experimental sample, not a production Gentoo package. Live build and integration must be verified on a KDE PIM 6 host.

## Architecture

`NewsResource : Akonadi::ResourceBase` implements collection discovery, item synchronization, and batch payload retrieval. `NewsSource` and `loadNews` implement the independent filesystem backend; `NewsArticle::message` converts each record to a KMime message. The resource is its own process registered via a desktop metadata file.

See [Gentoo host testing instructions](docs/testing-gentoo.md).

References: [GLEP 42](https://www.gentoo.org/glep/glep-0042.html), [Akonadi ResourceBase](https://github.com/KDE/akonadi/blob/master/src/agentbase/resourcebase.h), [KDE Maildir resource](https://github.com/KDE/kdepim-runtime/tree/master/resources/maildir).
