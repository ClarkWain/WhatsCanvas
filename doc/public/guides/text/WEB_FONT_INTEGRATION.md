# Web / asynchronous font integration

WhatsCanvas keeps remote I/O outside the rendering core. `RemoteFontProvider`
owns deterministic font-subset selection and lifecycle state, while the Web,
native, or application host owns HTTP, cache storage, cancellation, and its
event loop. The same API can therefore be used by a browser fetch adapter, an
iOS on-demand-resource adapter, or a desktop cloud-font service.

## Lifecycle

Each registered source moves through:

```text
IDLE -> QUEUED -> DOWNLOADING -> LOADED
                     |
                     +-> IDLE              (retryable failure)
                     +-> PERMANENT_FAILURE (explicit failure, retry cap, or budget)
```

`FontProvider::match()` never performs network I/O. A miss queues the best
source metadata for the requested family/style/codepoints and immediately
returns the already loaded faces, if any. Repeated matching does not duplicate
queued or active work.

## Host loop

Register metadata before attaching the provider to a `Canvas`:

```cpp
auto remote = std::make_shared<wsc::RemoteFontProvider>(
    wsc::FontProviderKind::DYNAMIC, "web-fonts");

wsc::RemoteFontSource latin;
latin.font.descriptor = wsc::FontDescriptor("App Sans", 400);
latin.font.sourceId = "https://cdn.example/fonts/app-sans-latin.woff2";
latin.font.fingerprint = "sha256:manifest-content-hash";
latin.font.codepointRanges.emplace_back(0x0000, 0x024F);
latin.expectedBytes = 48 * 1024;
remote->registerSource(std::move(latin));
canvas.addFontProvider(remote);
```

After layout or glyph lookup has queued work, the host drains requests without
blocking the render thread:

```cpp
for (const wsc::RemoteFontRequest &request : remote->takeDownloadRequests()) {
    startAsyncFetch(request.sourceId,
        [remote, id = request.sourceId, token = request.requestToken](std::vector<std::uint8_t> bytes) {
            remote->completeDownload(id, token, std::move(bytes));
        },
        [remote, id = request.sourceId, token = request.requestToken](bool permanent, std::size_t received) {
            remote->failDownload(id, token, permanent, received);
        });
}

// Once per event-loop turn or frame, never from inside layout/render:
const auto changedFamilies = remote->takeChangedFamilies();
if (!changedFamilies.empty()) {
    scheduleOneRelayoutAndRepaint(changedFamilies);
}
```

Only call `completeDownload` or `failDownload` after a request has been returned
by `takeDownloadRequests`, and echo its `requestToken`; completion for an
idle/queued/unknown source or a stale attempt is rejected. A successful
completion creates an in-memory `FontFace` and advances
the affected family generation, so portable and DirectWrite selection/render
caches stop reusing the earlier miss.

`takeChangedFamilies()` is the notification boundary: registration,
completion, permanent failure, invalidation, replacement, and removal are
deduplicated by canonical family until drained. Queue/download/transient-failure
state alone does not request relayout.

## Policy and limits

`RemoteFontProviderOptions` controls concurrent downloads, attempts per source,
candidates queued by one match, and a cumulative byte budget. `expectedBytes`
reserves budget while work is queued or downloading; actual successful bytes
and `failDownload(..., consumedBytes)` count against the cumulative total.
Setting the budget to zero disables it.

Codepoint ranges are trusted scheduling metadata, not proof that downloaded
bytes contain a glyph. Portable rasterization still performs real glyph
coverage checks. Sources without ranges are treated as broad fallback sources
and should normally have lower priority than script-specific subsets.

Set `LazyFontSource::fingerprint` to an immutable content revision or digest.
Replaying identical metadata then preserves a loaded face or active request;
changing the fingerprint cancels the old provider state and gives any new
attempt a different request token. When it is empty, re-registration remains
conservative and always invalidates the source.

## Optional browser adapter responsibilities

A browser host that chooses to support remote Web fonts should:

- ship a deterministic baseline font so first layout has stable metrics;
- execute fetches with cancellation and persistent HTTP/browser caching;
- convert WOFF/WOFF2 if the configured rasterizer cannot consume it directly;
- marshal completion to a safe event-loop/frame boundary;
- coalesce multiple completed fonts into one relayout/repaint notification;
- optionally mirror loaded faces into `document.fonts` when DOM text also uses
  them; Canvas-only rendering does not require that registration;
- expose diagnostics for offline, HTTP, decode, retry, and budget failures.

Do not enumerate browser-installed fonts as the main fallback strategy. It is
privacy-sensitive, browser-dependent, and produces nondeterministic results.
