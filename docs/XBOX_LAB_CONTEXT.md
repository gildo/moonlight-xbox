# Xbox Lab Client Handoff

Last updated: 2026-06-08

## Repository State

- Branch: `gildo/xbox-one-s-pacing-lab`
- Documented HEAD: `80f10facf20d6ddeeb4317c4e68f61c1edb9bec5`
- Installed package: `1.18.1.27`
- Production pacing commit: `9a72c57`
- Startup teardown hardening commit: `80f10fa`

## Unresolved P0

Physical controller selection of the host/app reportedly closes the client on
every attempt. Automated WDP navigation succeeds, so the remaining fault is
likely in controller-driven UI activation or queued page lifecycle work.

Audit and harden these paths together:

- `Pages/HostSelectorPage.xaml.cpp`: `GridView_ItemClick()` and `Connect()`.
- `Pages/AppPage.xaml.cpp`: `AppsGrid_ItemClick()`, `Connect()`, and asynchronous
  close-and-start completion.
- `Pages/StreamPage.xaml.cpp`: queued startup from `Page_Loaded()` versus
  `Page_Unloaded()` and navigation cache reuse.

Required properties of the fix:

- Only one navigation/stream launch can be committed per page instance.
- Duplicate controller activation is ignored and logged.
- A stale queued `Page_Loaded()` callback cannot construct `m_main` after unload.
- Unload/teardown remains idempotent.
- Navigation failure releases the guard rather than leaving the page stuck.
- Existing refresh-window pacing behavior and telemetry remain unchanged.

## Verification

1. Build one versioned package containing the complete re-entrancy fix.
2. Install with WDP while no stream is active.
3. Launch host and app ten times using the physical controller.
4. After each launch, confirm one `stream_start_requested`, one
   `connection_started`, a live Moonlight process, and no crash dump.
5. Run a short 3D Chaos proof and compare pacing metrics with the production
   baseline documented in the lab context.

Full operational context lives in
`/home/gildo/src/xbox-moonlight-lab/docs/CURRENT_CONTEXT.md`.
