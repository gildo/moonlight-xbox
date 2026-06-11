# Xbox One S Client Guidance

This checkout is the Xbox client component of
`/home/gildo/src/xbox-moonlight-project`.

Read these before changing stream behavior:

- `/home/gildo/src/xbox-moonlight-lab/docs/CURRENT_CONTEXT.md`
- `/home/gildo/src/xbox-moonlight-lab/docs/GOALS.md`
- `/home/gildo/src/moonlight-xbox/docs/XBOX_LAB_CONTEXT.md`

Current P0 is a repeatable app close/crash when the user launches a stream with
the physical Xbox controller. WDP coordinate-click automation does not reproduce
it and must not be treated as equivalent verification.

Preserve the production pacing behavior in `LabPacingConfig` while fixing UI
and stream lifecycle re-entrancy. Bundle host navigation, app launch, and
StreamPage lifecycle hardening into one build, then verify with repeated physical
controller launches before resuming performance tuning.

Builds are produced by GitHub Actions for branch
`gildo/xbox-one-s-pacing-lab`. Increment `Package.appxmanifest` for each package
that will be installed on the Xbox. Do not overwrite or discard unrelated user
changes.
