# ShaderCatalog provenance producer

ShaderCatalog observes D3D11 shader creation without changing the call result. It keeps
operational state in SQLite and finalizes each process run into one deterministic manifest.
It does not write a fallout4-re database or receipt.

Runtime shader creation is not proof that a shader was bound or executed. RenderDoc capture
remains required for executed-event evidence.

## Version contract

The producer exposes these constants in `Provenance.h`:

- Live SQLite schema: `kCatalogSchemaVersion = 3`
- Manifest schema: `fo4cs.shader-catalog-run`
- Manifest schema version: `kManifestSchemaVersion = 2`

An importer must reject a different manifest name, an unsupported manifest version, or a
newer live schema version. ShaderCatalog rejects a newer or malformed database without
resetting it.

Manifest-schema-v2 producers emit only schema-v2 documents. Consumers must accept immutable
manifest schema v1 and v2; v1 has no writer-cadence or subclass-attribution fields. The
publication path remains `runs/<generated-run-id>/manifest.v1.json` for all manifest schema
versions; its `v1` names the publication layout, while document `schema_version` names the
manifest schema.

Schema v3 preserves the v1/v2 `sessions`, `shader_catalog`, and `compile_events` tables as
legacy global state. New runs may continue to update those operational tables, but no
historical legacy row is assigned to a v3 run. Per-run evidence exists only in:

- `catalog_runs`
- `catalog_run_quality`
- `catalog_content_identities`
- `catalog_run_observations`
- `catalog_run_hresult_details`
- `catalog_run_attributions`
- `catalog_run_blobs`

Migrations use one checked transaction, keep foreign keys enabled, and preserve WAL mode.
Structural and foreign-key checks run before commit, and the schema version is written last.
Operational and final checkpoints are checked `PASSIVE` checkpoints.

## Hook surface

ShaderCatalog observes these `ID3D11Device` slots:

- 12: `CreateVertexShader`
- 13: `CreateGeometryShader`
- 14: `CreateGeometryShaderWithStreamOutput`
- 15: `CreatePixelShader`, owned by `PixelShaderSwapBroker`
- 16: `CreateHullShader`
- 17: `CreateDomainShader`
- 18: `CreateComputeShader`

Each hook acquires a run-scoped producer lease before preparing bounded bytecode and
descriptor identity. The lease remains active across the original call, resolver, and
completion enqueue. Admission closes race-safely during Stop, which then waits for every
lease with an atomic wait before draining. Pixel observer admission is independent of token
allocation, so allocation failure cannot release the lease before the original call. The
original arguments, output, and return value are preserved.

Hook coverage becomes ready only after observer registration succeeds and every detour exposes
a non-null original function pointer for device slots 12 through 18, including the broker-owned
slot 15, plus `PSSetShader` slot 9. A partial or failed installation remains non-ready and
non-authoritative.

The bytecode bound is 16 MiB. Null, empty, oversized, allocation-failed, guarded-copy-failed,
and hash-failed submissions are counted and represented explicitly. Guarded Windows reads
also protect stream-output declarations, strides, and semantic pointers. Exact bytes receive:

- SHA-256 as the stable content identity
- SHA-1 as compatibility metadata

Stream-output identity uses the binary encoding tag
`fo4cs.d3d11-stream-output.v1`. It includes every declaration field, bounded semantic
content and null/empty/truncated state, buffer strides, and rasterized stream. Pointer
addresses are never identity. Its state is explicit: `not_applicable`, `exact`,
`unsupported_size`, `allocation_failure`, `copy_failure`, `hash_failure`, or
`metadata_truncated`. Every non-exact applicable state gates authority.

The hook queue is bounded and non-blocking. SQLite, reflection, raw publication, and
manifest generation run off the creation thread.

## PixelShaderSwapBroker ordering

The broker remains the sole slot-15 owner and ordered resolver registry. Its order is:

1. Observer preparation over stock submitted bytecode
2. Original `CreatePixelShader`
3. Original-object observer registration
4. Ordered resolver invocation, when eligible (bytecode patches before HLSL)
5. Completion observer for every result, including failure and null output

The original object is tracked before replacement. A final alias is registered only when
the final pointer differs from the original pointer. Resolver invocation, resolver-reported
replacement, and final stock/replacement/null classification are separate counters.
Passthrough is not an alias. A replacement digest remains null unless a future resolver
honestly supplies replacement bytecode metadata. The stock digest is never reused as a
replacement digest.

A usable stock or final object requires a successful `HRESULT`, a requested output, and a
non-null post-call pointer. Failed results with stale pointers are retained as raw outcome
metadata but never classified as stock or replacement. Null-output counts exclude calls that
did not request an output. Every unusable outcome retains bounded HRESULT details, including
successful HRESULT values paired with requested null output. Overflow is explicit through
`other_hresult_count`, `hresult_details_truncated`, and the metadata quality gate.

## Environment contract

ShaderCatalog reads these values once when the run starts:

| Variable | Contract |
|---|---|
| `FO4CS_SHADER_CATALOG_EVIDENCE_MODE` | Optional. Exactly `true` or `false`, lowercase. Any other value is invalid. |
| `FO4CS_SHADER_CATALOG_RUN_ID` | Optional external orchestration ID. Required in evidence mode. |
| `FO4CS_SHADER_CATALOG_SCENARIO_ID` | Optional outside evidence mode. Required in evidence mode. |
| `FO4CS_SHADER_CATALOG_CONFIG_ID` | Optional configuration ID. |
| `FO4CS_SHADER_CATALOG_SOURCE_ID` | Optional source/build orchestration ID. |
| `FO4_SHADER_CORPUS_ROOT` | Optional absolute, existing, caller-owned directory. Setting it opts into raw DXBC export. |

IDs are 1 through 128 ASCII characters from `[A-Za-z0-9._:-]` and may not contain
`..`. Invalid or missing evidence IDs are stored as null, are never invented, and make the
run non-authoritative.

A present empty value is invalid for every variable in this table. It is not treated as
absent or as disabled export.

`FO4_SHADER_CORPUS_ROOT` is rejected if it is relative, missing, not a directory, or has a
Windows reparse point in its path. Invalid requested export is a durable quality failure.

## Lifecycle and quality

Every process gets a cryptographically generated internal UUID. A run starts as `running`.
Startup changes every prior v3 `running` row to `abandoned` and increments its lifecycle
quality counter, except a verified same-root pending publication, which startup promotes.
Only a completed in-process close with its publication, or that conservative reconciliation,
can establish `finalized`.

Startup has one commit point: the transaction that inserts the run row. Every fallible
prerequisite runs before it, in order: database open and migration, prior-run recovery,
long-lived statement preparation, stored run identity, and the writer thread, which is
constructed but held at a gate. A failure at any of those steps aborts and joins the gated
writer, rolls back any open transaction, closes the database, and returns false, leaving no
durable state for the new run. Schema migration and prior-run recovery are deliberately durable
and are unaffected. Only after that commit succeeds does startup publish lifecycle,
producer admission, and service state, and only then is the writer released. That tail is
nonthrowing atomic publication plus the gate release, so a half-started run is never
observable and a committed run never lacks its writer. A finalization that raises runs
fail-closed cleanup instead: it cannot leave an authoritative catalog context. One lifecycle
mutex serializes complete
start and stop operations, so concurrent callers cannot interleave into shared handles; it is
never taken by producer admission, enqueue, or the writer that a close waits on.

When the enabled catalog starts, one checked Microsoft Detours transaction covers both normal
process-exit paths: `Kernel32!ExitProcess` and `ntdll!RtlExitUserProcess`. Targets are normalized
before installation; if both exports resolve to the same code, one physical detour covers both
logical paths. Any pre-commit attach failure aborts the whole transaction. That transaction runs
only after the close coordinator worker is live, so an installed callback is always serviceable,
and readiness is published only after both paths are covered and the callback is visible. Both
thunks share one idempotent gate, request the close coordinator, then chain their exact original
no-return target. The hooks are process-lifetime and are not detached during DLL teardown. They
are fallback requesters; they are not the authority for finalization.

An enabled run owns one in-process close coordinator. Its state is one-way: `inactive`,
`capturing`, `closing`, `finalized_inert`, plus `aborted` for a failed startup. The worker is
established before the database run, admissions, or any exit callback exists; if it cannot start,
none of those are created. If database startup then fails, the idle coordinator is rolled back to
`aborted`, which joins the worker, drops the close action, and refuses every later run or request.
One coordinator-owned worker is the only caller of `CatalogDB::Stop`. The optional capture
deadline, the ShaderCatalog UI button, and both exit thunks only request that close, and repeated,
reentrant, and concurrent requests coalesce into exactly one `Stop`. No request runs finalization
on a shader, observer, render, ImGui, or exit thread, and every fallback wait is bounded. After
the sole `Stop` returns, the run is inert: the tracker is disabled, later create, bind, and generic
producer admissions are rejected without changing published evidence, and no run is reopened in
process.

Finalization must reach its publication decision within a 30 second monotonic budget. Sealing that
decision is the only authority latch: it happens exactly once, after every unbounded drain and
before any publication, and it latches a veto when the budget has already elapsed. Because the
clock is monotonic, an overdue decision that is still open stays overdue and is latched by that
later seal. Observers never mutate the decision; a latched decision reads as timed out, a sealed
decision never does, and an open decision is reported as timed out only while the close is overdue.
A latched timeout is durable and is never promoted by later completion: it increments the catalog
`lifecycle_failure` quality counter, keeps the run non-authoritative, is reported exactly in
coordinator telemetry, and vetoes route manifest authority without adding a route wire reason.
Remaining in `closing` with admission shut is acceptable; external termination before publication
still produces no authoritative manifest and the next startup stays conservative.

The ShaderCatalog destructor is nonblocking and performs no database or tracker teardown.
Explicit DLL unload is unsupported. DLL unload, crashes, `TerminateProcess`, last-thread
termination, and other abnormal termination before the close completes leave the run `running`;
the next startup marks it `abandoned`. Authority comes from a completed in-process close and its
publication. Which covered exit path a runtime takes is diagnostic only and never establishes
authority by itself.

Finalization performs this sequence:

0. Snapshot hook coverage, orderly-finalizer readiness, route combined hook coverage, and graphics
   identity at the close cutoff, before lease admission closes
1. Close lease admission and wait for all in-flight shader calls
2. Drain and commit the queue with bounded transaction retries, completing deferred shape and
   requested raw publication work as the writer finishes and joins, then snapshot drain state and
   durable quality
3. Seal the finalization decision once and carry any latched timeout veto into route and run
   authority
4. Publish route observations and the route manifest under that sealed decision
5. Query final state and stage a verified manifest under a non-contract temporary name
6. Persist quality, legacy state, final run state, hook readiness, and publication intent
7. Perform the checked final `PASSIVE` checkpoint and close SQLite
8. Rename to `manifest.v1.json` as the last fallible authoritative operation

Close inputs are phased, and each becomes immutable at its own point. Readiness, graphics, and
runtime identity freeze at the cutoff in step 0; the resolver registry freezes at the route capture
freeze; drain state and durable quality freeze only after admitted producers have drained and the
writer has joined, so a producer admitted before the cutoff still counts if it settles before the
drain completes. Once sealed, that context is the only source for the quality veto, catalog
authority, route and main manifest fields, the final database row, and publication. Nothing rereads
live readiness, the route hook provider, the registry, or quality afterwards. Readiness or provider
state that changes after its snapshot is ignored in both directions: it can neither promote a vetoed
run nor demote a clean one. Producer work rejected after the cutoff never reaches any of it.

Route capture admission is not a second gate. Route create and bind acquire the same producer
admission that generic work uses, and hold it for the whole token lifetime, so the single
`fetch_or` that closes producer admission linearizes generic and route entry at one instant and no
route token can appear after it. Because a live token keeps that admission, the producer drain also
waits for route tokens, and the publisher can only be waited on, frozen, and released afterwards;
no route token can hold a raw publisher pointer across that release.

Every deferred preparation that can affect authority, and every veto input, completes before the
Seal, and the Seal precedes route publication. This is a within-process sequence only. It is not a
cross-file atomic commit: route and catalog artifacts stay independent, as Authority composition
describes.

If any pre-publication gate fails, no contract manifest appears. If the final rename fails,
the database is reopened only to mark the run abandoned and persist manifest/lifecycle
failure. `publication_pending` plus a manifest digest, size, and artifact-root fingerprint
allow the next startup under the same root to verify a crash window. While pending, the DB
row remains `running`, `authoritative=0`, and `manifest_published=0`; the verified artifact
and immutable cached snapshot may already report finalized authority. A same-root startup
promotes a matching pending row while retaining its verified file and directory handles
through the database commit. Missing, mismatched, or different-root pending artifacts are
abandoned conservatively. The schema enforces that a DB-authoritative row is finalized,
published, and not pending.

Quality is durable per run and includes queue overflow, malformed bytecode, unsupported
size, allocation/copy/hash failure, metadata truncation, database write failure, raw export
failure, manifest failure, hook/observer gaps, writer drain failure, lifecycle failure, and
configuration failure.

A run is authoritative only when all of these are true:

- Its lifecycle is `finalized`
- Evidence-mode IDs are satisfied and the environment contract is valid
- The writer drained
- Every quality loss/failure counter is zero
- Requested raw exports completed
- The manifest was published
- The validated process-exit hook was installed and `orderly_finalizer_ready` is true
- All VS/GS/GSSO/PS/HS/DS/CS and broker installation paths completed and
  `hook_coverage_ready` is true

Export-disabled is not a failure.
Requested raw export is complete only when every exact unique content identity observed in
the current run has a current-run association to its canonical blob path.

## Artifact layout

When `FO4_SHADER_CORPUS_ROOT` is valid, both the manifest and requested raw blobs are
published below that root:

```text
runs/<generated-run-id>/manifest.v1.json
blobs/sha256/<first-two-hex>/<64-lowercase-sha256>.dxbc
```

Without `FO4_SHADER_CORPUS_ROOT`, raw export is disabled and the manifest is published
under `shader-catalog-artifacts` beside the configured SQLite database. This keeps normal
catalog runs complete without making proprietary byte export implicit.

Blob files contain the exact stock bytes submitted to D3D11. Blob paths are associated with
the current run in `catalog_run_blobs`; export-disabled runs never inherit another run's
association. The manifest accepts a stored path only when it exactly equals the canonical
path derived from that row's SHA-256.

Publication uses atomic create-without-overwrite, flush, and rehash. Existing content is
accepted only when size and SHA-256 match. Corrupt existing content fails rather than being
replaced. Identical concurrent publishers verify and accept the winner; differing collisions
fail. A loser boundedly retries sharing or lock collisions while a just-renamed identical
winner still retains its no-write/no-delete-share handle. Windows publication pins every
ancestor and publication directory without delete sharing, rejects reparse objects, verifies
final handle paths beneath the pinned root, retains target handles through publication, and
renames a verified temporary handle. Traversal is rejected.

The manifest is locale-independent canonical UTF-8 JSON with a final LF, fixed object key order, and sorted
blob, observation, HRESULT, and attribution arrays. It contains:

- Generated, external, scenario, config, and source IDs
- Writer flush cadence plus subclass-attribution state. `requested` is the config setting;
  `enabled` means requested and matched to a verified BSShader layout before hook installation.
  It does not guarantee that attribution events were produced.
- Runtime family/version, plugin semantic version, build describe, git identity, PID, and
  honestly available adapter/feature-level facts
- Lifecycle, authority, export, drain, hook coverage, orderly-finalizer readiness, and quality
  state
- Per-run attempts, successes, failures, unique observations, and unique content
- Content hashes, relative blob paths or null, and reflected shapes
- Per-observation stage, bytecode and stream-output state/identity, bounded HRESULT details,
  first/last sequence and QPC, thread/module/stack attribution, and resolver classification
- Per-run subclass and technique attribution with explicit `creation_context`,
  `observed_binding`, or `technique_map_association` attribution kind and `stock`,
  `replacement_unknown`, `originating_stock`, or `submission_no_object` object kind.
  Technique value zero is retained.

It contains no native bytecode, pointers, absolute source paths, machine source paths, or
absolute blob paths.

## fallout4-re importer handoff

The handoff unit is exactly
`runs/<generated-run-id>/manifest.v1.json` plus any relative blob paths named by that
manifest. A fallout4-re importer must validate the manifest schema/version and all supplied
blob SHA-256 values before importing. It may then create consumer-side receipts. Community
Shaders never opens or writes the fallout4-re database and never creates importer receipts.

Creation outcomes can establish submitted content, creation multiplicity, and object
replacement classification. They cannot establish binding or execution. Join this artifact
with RenderDoc executed-event evidence on the consumer side when execution proof is needed.

## Subclass attribution

Known `BSShader` reload/setup surfaces and the `PSSetShader` fallback still map tracked
pixel-shader objects to subclass and technique IDs. Runtime layouts that have not been
verified skip subclass hooks without disabling creation observation or the swap broker.
Attribution is per run in schema v3. The legacy global row remains a compatibility view.
Replacement aliases retain their originating stock identity internally, but an observed
replacement binding emits a null shader identity plus `originating_stock_sha1`; it is never
presented as a binding of the stock object itself. SetupTechnique probing is a
`technique_map_association`, not an observed binding. Failed, null, and no-output creation
contexts use `submission_no_object`. A reused replacement pointer associated with more than
one stock SHA-1 is ambiguous: singular origin is omitted and durable quality prevents
authority rather than selecting an arbitrary stock identity.

## Stock runtime route receipts

Route receipt capture is a separate, disabled-by-default producer. It starts only when
`settings.route_receipt_capture=true` and `settings.route_receipt_output_root` names an
explicit absolute, existing, non-reparse directory. There is no environment, current-directory,
temporary-directory, or repository fallback.

`settings.route_receipt_capture_duration_seconds` bounds one capture window. It defaults to 90,
is validated to 1 through 3600, is persisted with the other settings, and is inert while route
capture is disabled. The window arms once, on a monotonic clock, and only after device hook
installation reports complete coverage, `hook_coverage_ready` is established, and the route
publisher is open. At the deadline the coordinator closes and finalizes the run in process while
the game keeps running; the UI button requests that same one-way close earlier. Capture never
depends on observing process exit.

The wire contract is `fo4cs.stock-runtime-route-observation` v1 plus
`fo4cs.stock-runtime-route-run-manifest` v1. Canonical files use sorted-key compact JSON with
one final LF and publish without replacement at:

```text
<root>/observations/<document-sha256>.json
<root>/manifests/<document-sha256>.json
```

The fixed scenario is `stock-pixel-shader-routes-v1`: pixel-stage broker callbacks from every
subclass with one scoped `SetupTechnique` context. Every callback is included or counted under one
closed exclusion reason before allocation. Capture leases close atomically and drain before the
tracker snapshot. Observation publication then uses a separate admission gate.

Each observation binds:

- Exact plugin binary and Fallout 4 executable SHA-256 identity
- On-disk plugin hook and diagnostic-resolver x64 unwind ranges, with ASLR-safe RVAs
- Scoped subclass and raw technique
- Nullable direct tiled-lighting state
- Diagnostic `plugin_resolved_psid` and its plugin resolver identity
- Literal null `observed_lookup_psid` and `engine_lookup_observed=false`
- Exact pre-original stock byte length, SHA-1, and SHA-256
- Original creation, class-linkage, final-stock, and resolver-invocation facts
- One run-local non-pointer object ID and one later matching `PSSetShader` bind

The broker snapshot used for observer evidence is the same snapshot used for resolver dispatch.
This does not make the diagnostic PSID an observed engine return. Fallout4-re derives
`engine_lookup_psid` only from separate native evidence.

Stock-only authority requires an empty resolver registry at open and close with unchanged monotonic
generation and canonical registry digest. Resolver invocation, changed original input, or a
successful non-stock final object makes the run non-authoritative. Failed, null-output, unbound,
ambiguous, duplicate, and route-mismatched records remain visible but only make their own record
non-authoritative when run accounting is otherwise complete.

A close that misses the finalization budget vetoes route manifest authority without adding a wire
reason. The manifest keeps its closed derived reason set: when derived reasons exist it publishes
exactly those, and when none exist it publishes the contract fallback `producer-declined`. The
exact timeout fact stays in coordinator telemetry and in the catalog `lifecycle_failure` counter.

The plugin retains only successful, non-null final stock objects for route lineage. Failed stale
pointers, replacements, null shaders, untracked binds, and repeated terminal binds receive no route
event. Bind observation proves API binding only. It does not prove draw submission, GPU execution,
presentation, or fidelity.

Plugin route documents are archive-blind. Their typed API has no archive, FXP, shader-key, ordinal,
offset, occurrence, corpus, participant, recipe, policy, admission, verdict, draw, or fidelity field.
Archive occurrence selection and route-join verdicts remain fallout4-re responsibilities.

## Authority composition

Observation `capture_authoritative` is row-local: it states only that this record's own
accounting and stock-only conditions hold. Route manifest `capture_authoritative` is
route-local: it states only that this capture run's own membership and accounting hold.

Downstream promotion requires route membership authority, which is the manifest and the row
together, plus an authoritative enclosing catalog context with exact run and build binding:

```text
membership = route_manifest.capture_authoritative AND observation.capture_authoritative
promotion  = membership AND authoritative enclosing catalog context, exact run/build binding
```

That promotion gate belongs to fallout4-re. Community Shaders publishes route-local facts and
never asserts catalog authority from a route document. A false route manifest may still contain
true rows, and no row is promotable on route facts alone.

A structurally valid route artifact left behind by process death or by a failed catalog
finalization is an orphan, never a final accepted handoff. Route documents publish before
catalog persistence, the final checkpoint, and the contract manifest rename, and that plugin
publication order is not proof of cross-artifact trust: the two roots are independent files and
are not atomically committed. An orphan is rejected because its enclosing catalog context is
non-authoritative, not because of any ordering claim.

Coordinator telemetry is a packed lock-free word, refreshed only when the coordinator publishes
a status or a caller takes a snapshot. Read before the coordinator observes the clock it can lag
the monotonic budget and still report an overdue close as not timed out. That lag is diagnostic
only and never establishes, delays, or removes authority. Sealing the finalization decision is
authoritative.
