# Repository Format 6 and Key Rotation

**Working Document!!!** This spec guides development and will be removed before the final commit. Anything durable (user-facing behavior, the format feature table, migration guidance) moves to the user documentation before this file is deleted. The work will probably land as two commits -- the repository format infrastructure (adopting new formats at `stanza-create` and `stanza-upgrade`) and the key rotation feature built on it -- so the sections are grouped to allow the format infrastructure to be pulled out into its own spec later. Key pruning at expire is also in scope for the release and may land as a third commit.

## Goals

- Introduce repository format 6 as a single opt-in boundary that bundles format changes, rather than adding an option per feature.
- Provide a migration path for existing stanzas via `stanza-upgrade`, so large repositories can adopt format 6 without being recreated.
- Enable key rotation: an archive sub-passphrase per archive-id, a single sub-passphrase per backup set covering its manifests and data files, optional user passphrase rotation, and automatic archive rotation on a schedule.

## Current State (Format 5)

### Format handling

- `REPOSITORY_FORMAT` is a compile-time constant (`src/version.h:31`) stored as `backrest-format` in every info file and manifest.
- Any mismatch at load is a hard `FormatError` (`src/info/info.c:182`), raised before any data is read.

### Encryption envelope

- `repo-cipher-pass` (user-supplied) encrypts `backup.info` and `archive.info`.
- `backup.info` holds a sub-passphrase that encrypts every `backup.manifest` (and `.copy`).
- Each manifest holds a sub-passphrase that encrypts the data files of its backup set. A new sub-passphrase is generated per full backup (`src/command/backup/backup.c:244`) and inherited by diff/incr backups (`src/command/backup/incr.c.inc:146`), so one sub-passphrase covers an entire backup set and expires with it. This level of the hierarchy already rotates.
- `archive.info` holds a single stanza-wide sub-passphrase that encrypts all WAL across all archive-ids for the life of the stanza -- the only bulk-data key that never rotates.
- Sub-passphrases are 48 random bytes, base64-encoded to 64 characters (`cipherPassGen()`, `src/command/stanza/common.c:19`).

## Repository Format Infrastructure

### Format option

- New repo-indexed option `repo-format` (so `repoN-format` in config), consistent with `repo-cipher-type`.
- Valid only for `stanza-create` and `stanza-upgrade` (see Migration); every other command derives the format from repository state. Not required after creation since the repository is self-describing.
- Allowed values: 5, 6. Default remains 5 when format 6 is introduced.
- Error message when a binary is too old for the repository: `repository format 6 requires pgBackRest X.Y or later` (X.Y = the release that introduces format 6). Older released binaries will report `expected format 5 but found 6`, which fails cleanly at info load before any data is touched.

### Format handling rules

- **Readers trust the per-file stored format.** Every manifest and info file already records `backrest-format`; loaders branch on the stored value, not a global constant.
- **The info file format is the write target.** `backup.info` / `archive.info` format determines the format of new WAL and of new backup sets. Individual backups within a stanza may be older formats until they expire.
- **Backups adopt a new format only at a full backup.** After `stanza-upgrade --repo-format=6`, diff/incr backups inherit the format of their set from the prior manifest (parallel to sub-passphrase inheritance, `src/command/backup/incr.c.inc:146`), so a backup set is never mixed-format. The full backup is thus the single adoption boundary for the backup domain: format and the set sub-passphrase change only there.
- Consequence: once the info files are format 6, binaries that only support format 5 cannot read the stanza at all, including its remaining format 5 backups. This is accepted; the info files gate everything.

### Migration

- `stanza-upgrade --repo-format=6` flips the write target on an existing stanza. This ships in the first format 6 release; the loader's per-file format reading (above) supports it from the start.
- `stanza-upgrade` is the intentional adoption boundary: it already rotates the archive-id, and a full backup already starts a new backup set with a fresh sub-passphrase, so both data domains have natural points where the new format begins.
- Old backup sets remain format 5 until expired; old archive-ids remain format 5 until expired.
- This also allows key rotation features to be adopted opt-in on existing repositories without recreation.

### Default format bump

- The `stanza-create` default moves from 5 to 6 in a designated future release, announced when format 6 first ships. Roughly a year out -- about four releases that support format 6 before the default flips -- so a buggy release always leaves supported fallbacks. Release-tied, never wall-clock-tied: the same binary and config must always produce the same repository.
- The default flip is the point where mixed-version deployments can break on new stanzas (new repo host writes format 6, old db host cannot read it). It gets a prominent release note, and `--repo-format=5` remains the explicit escape hatch for as long as format 5 is writable.

## Key Rotation

Rotation is performed by a new standalone `stanza-rekey` command plus two automatic paths that need no command at all: the backup set sub-passphrase rotates with every full backup, and the archive sub-passphrase rotates on a schedule during expire. `stanza-rekey` is repo-only by design -- no cluster access -- so keys can be rotated during incident response with the cluster down or unreachable. This is why it is not a flag on `stanza-upgrade`, which requires cluster access for `pgValidate()` (`src/command/stanza/upgrade.c:38`).

A `stanza-rekey` run rotates the archive sub-passphrase immediately by starting a new archive-id, and converges the user passphrase when the user has changed it in config (per Operational design) -- optional in keeping with the original feature request, since it is harder to automate (it requires a new user-supplied secret) and has a smaller attack surface (it wraps only the info files). The backup set sub-passphrase needs no rekey action since it rotates at every full backup.

### Archive sub-passphrase per archive-id

- Format 6 stores a sub-passphrase per archive-id in `archive.info`, created with the archive-id and stamped with its creation time. Every file in an archive-id -- WAL segments, `.partial`, `.backup`, and timeline `.history` files alike -- uses that id's key: exact lookup, no boundary comparison, no trial decryption.
- Rotation starts a new archive-id with an unchanged PostgreSQL version and system-id: a new `db:history` id recorded in `archive.info` and `backup.info` together, since `checkStanzaInfo()` errors when the current ids diverge (`src/command/stanza/upgrade.c:82`). A PostgreSQL upgrade via `stanza-upgrade` rotates naturally the same way.
- Running archivers cannot conflict with rotation: a pusher still holding the previous `archive.info` keeps writing to the previous archive-id with that id's key, which remains fully consistent -- late gap fills included. On its next load it sees the new archive-id and switches. archive-push writes only to the current archive-id (`src/command/archive/push/push.c:242`) and archive-get already searches every archive-id matching the current version and system-id (`src/command/archive/get/get.c:428`), so no fallback logic is required anywhere.
- A same-content repush that lands in a newer archive-id than the original is benign: get collects matches across archive-ids and dedups by the content hash in the stored name, erroring only when content differs (`src/command/archive/get/get.c:280`).
- Cross-archive-id PITR is expected to work today: get searches all candidate ids per request, and unit coverage exists for same-version ids (`test/src/module/command/archiveGetTest.c:262`). Any missing coverage -- notably an end-to-end recovery replaying WAL across an archive-id boundary -- is written and committed separately, ahead of the rotation work.
- To confirm during implementation: every consumer of `db:history` copes with repeated version/system-id entries -- expire per-id retention (including WAL in a new archive-id that has no backups yet but is needed for PITR from prior-id backups), info display, and backup db-id assignment.

### Backup set sub-passphrase

- Format 6 stores a single sub-passphrase per backup set in `backup.info`, keyed by the full backup label, and it encrypts both the set's manifests and its data files. Every full backup generates a fresh entry, so rotation is automatic and needs no option, marker, or age tracking.
- This replaces two format 5 levels at once: the stanza-wide manifest key and the per-set data key embedded in the manifest. The split had no security value -- the data key is stored inside a manifest encrypted with the manifest key, so holding the manifest key always transitively unlocked the data -- and once the manifest key is per-set, they are the same key. Format 6 manifests drop the `[cipher]` sub-passphrase field; format 5 manifests keep theirs and are read per stored format.
- Lookup is exact, not ordered: a diff/incr label embeds its full's label as the prefix before `_`, so the key for any manifest or data file -- current or under `backup.history` -- is found by parsing the backup label. No boundary comparison exists anywhere in the design. The prior-manifest inheritance of the data key (`src/command/backup/incr.c.inc:146`) is no longer needed for format 6 sets.
- The entry is recorded at label assignment, before the first manifest copy is saved, so a resumed backup finds its key by the normal lookup with no special cases -- the data key included, which today comes from the resumed manifest copy. An entry from an aborted full is inert and is pruned once no manifest references it.
- A migrated stanza retains the format 5 stanza-wide manifest key in `backup.info` until the last format 5 manifest expires; format 5 sets keep reading their data keys from their manifests as always.

### Sub-passphrase format

- Format 6 generated sub-passphrases are 256-bit (32 byte) random keys, stored base64-encoded (44 characters) and decoded back to binary before being passed to `CipherBlock`. Format 5 generates 48 random bytes stored as 64 base64 characters and passes the encoded text itself as the passphrase (`cipherPassGen()`, `src/command/stanza/common.c:19`); the extra entropy adds nothing, since AES-256 caps effective strength at 256 bits and these keys come from OpenSSL's DRBG, not user input.
- Format 6 also switches the `EVP_BytesToKey` digest from SHA1 to SHA256. The per-object envelope is otherwise unchanged: `Salted__` magic (or raw mode), stored salt, and derivation still produce a unique key+IV per object. With a 32-byte key the derivation is tidier as well -- the first SHA256 digest is exactly the AES-256 key and the second supplies the IV, where SHA1 needs three chained digests. `CipherBlock` already accepts a digest parameter defaulting to SHA-1 (`src/common/crypto/cipherBlock.h:21`), including through the filter protocol, so no new plumbing is needed.
- The digest follows the key style, keeping selection deterministic per object with no trial decryption: text passphrases use SHA1, binary keys use SHA256. A migrated archive-id therefore stays internally consistent -- gap fills into it continue under its text passphrase with SHA1 -- and the info files, encrypted with the always-text user passphrase, remain readable before their stored format is known, avoiding a bootstrap problem and keeping manual `openssl enc -md sha1` decryption of the info files working as today.
- Keys carried over by migration -- the current archive-id's passphrase and the format 5 manifest key -- remain format 5 style. The two styles are unambiguous by length (64 characters of text passphrase vs 44 characters of encoded binary key), so the loader needs no new field, though an explicit per-entry marker is acceptable if preferred during implementation.
- The user passphrase (`repo-cipher-pass`) is unaffected: it is user-supplied and remains a text passphrase.

### Info file write coordination

- Backup currently rewrites `archive.info` at completion purely to freshen its timestamp so object-store lifecycle settings do not remove it early (`src/command/backup/complete.c.inc:241`). This resave moves to expire, which runs daily in a healthy deployment; backup stops writing `archive.info` entirely.
- archive-push and archive-get never write `archive.info` -- pushers are pure readers, so rotation needs no coordination with them beyond the atomic info save: a reader sees the old or the new file, and both are valid per the running-archiver rule above.
- Every info file writer -- backup and expire for `backup.info`, expire for `archive.info`, the stanza commands for both -- requires the backup lock, directly or as part of `lock-type: all`, and sets `lock-remote-required` (`build/config.yaml:65`, `build/config.yaml:77`) so the lock is acquired on the repository host whenever the repo is reached through one. `stanza-rekey` uses `lock-type: backup` with `lock-required` and `lock-remote-required`, matching backup and expire rather than the other stanza commands' `lock-type: all`: pushers never write the info files, so excluding the archiver would be awkward coordination -- rekey blocked whenever the async pusher holds the archive lock -- for no correctness benefit. Its archive-id mint under the backup lock is exactly the operation expire's timed rotation performs. The repository host's lock directory is thus the single serialization point for all info file writers regardless of which host each command runs on -- currently our best protection.
- With directly-reached storage (object stores, shared NFS) and writers invoked from multiple hosts there is no common lock point. This limitation predates this work and applies to backup and expire today; the documented practice remains to run backup, expire, and `stanza-rekey` against a given repo from one place. Storage-level conditional writes (S3 `If-Match`, Azure ETags, GCS generation preconditions) could close it for good someday; out of scope for this release.

### Automatic rotation

- A new option (tentatively `repo-cipher-rotate`, in days; unset means disabled) rotates the archive sub-passphrase automatically once the current archive-id's key is older than the period. It applies only to the archive key: the backup set key rotates with every full backup, and the user passphrase cannot be rotated automatically since it is user-supplied.
- The check runs in expire, which is expected to run at least weekly and typically daily via backup's expire phase: when the current key's age exceeds the period, expire starts a new archive-id exactly as `stanza-rekey` would. Rotation latency is bounded by expire cadence, negligible against a period measured in weeks. If expire never runs, neither does rotation -- the option depends on a normally-scheduled expire.
- A stanza migrated to format 6 stamps its existing key at migration time, starting the rotation clock there.

### User passphrase rotation

- Because of the envelope structure, `repo-cipher-pass` wraps only the info files. Rotating it re-encrypts four small objects (`backup.info`, `archive.info`, and their `.copy` files) and never touches bulk data. This does not strictly require format 6 (it is envelope-only), so it could ship earlier if wanted; it is grouped here because the documentation story is cleaner alongside the other rotation work.
- The optionality is automatic with the config-first flow below: when `repo-cipher-pass` is unchanged, convergence is a no-op and `stanza-rekey` rotates only the archive sub-passphrase.

### Key pruning at expire

- In scope for the format 6 release, though it can land as a commit separate from the rotation work.
- Archive: a key lives and dies with its archive-id. Expire already removes archive-ids that no retained backup needs; the key entry goes with the id. No new pruning logic beyond dropping the entry.
- Backup set: an entry serves exactly one set, so it is dropped when the last manifest of that set -- current or under `backup.history`, which can outlive the set under `repo-retention-history` -- is removed. Expire performs both removals, so it knows when that happens. The entry for the newest set is never pruned.
- Unpruned entries are only wasted bytes, so skipping pruning is always safe.
- All pruning happens in expire's existing info file rewrites under the backup lock; no additional locking is required (see Info file write coordination).

### Operational design

- Neither passphrase can appear on the command line: `repo-cipher-pass` is already a secure option (`build/config.yaml`, enforced at `src/config/parse.c:1807`) and the old-passphrase option is secure as well. Both come from the environment or config.
- Config-first flow: the user sets `repo-cipher-pass` to the new passphrase and `repo-cipher-pass-old` to the old one, then runs `stanza-rekey`, which converges each info file to the configured passphrase (decrypt with new, then old; rewrite anything still under old). Convergence is idempotent per file, so any failure or restart is handled by re-running. There is no point in the sequence where a needed passphrase is absent from config.
- General fallback: all commands retry an info file load with `repo-cipher-pass-old` when decryption fails with the primary passphrase, warning when the fallback is used. Config can then be updated across hosts non-atomically. In repo-host architectures only the repo host holds the cipher settings (remotes pull them down), so rotation is usually a single config edit.
- `repo-cipher-pass-old` is permanently supported for continuity; removing it is the user's choice. The fallback warning is the signal that it is still needed: the transition is over when `stanza-rekey` has completed and normal operations run without the warning, at which point the old passphrase can be removed from config.
- `stanza-rekey` serializes against other operations with `lock-type: backup` and `lock-remote-required`, like backup and expire (see Info file write coordination); it does not take the archive lock since archivers only read the info files. In particular it cannot start while a backup or expire is running. A backup that completes after the config update but before the rekey still saves `backup.info` under the passphrase it loaded at start, which the rekey then converges.

## Documentation Plan

- The `repo-format` option reference carries a table of features per format version -- the one place users look to answer "what do I get at format 6".
- Migration guidance (stanza-upgrade path, mixed-format stanzas, old-binary behavior) goes in the user guide.
- Release notes announce the introduction and, later, the default flip.
