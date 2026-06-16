# Woodpecker CI (Forgejo)

CI for building this QMK fork's NuPhy Halo75 V2 firmware on
[Woodpecker CI](https://woodpecker-ci.org/) integrated with Forgejo.

## Pipelines

| File            | Trigger                              | What it does |
|-----------------|--------------------------------------|--------------|
| `build.yaml`    | **manual** run / **pull request**    | Compiles every target. Publishes nothing - safe to run for testing. |
| `release.yaml`  | git **tag** push (e.g. `v1.2.3`)     | Builds firmware, creates a Forgejo **release** for the tag with the binaries attached. |
| `nightly.yaml`  | **cron** job named `nightly`         | Builds firmware, creates a dated `nightly-YYYY-MM-DD` **pre-release**. |

All three build the same targets directly in the Woodpecker pipeline:

- `nuphy/halo75_v2/ansi:default`
- `nuphy/halo75_v2/ansi:via`
- `nuphy/halo75_v2/iso:default`
- `nuphy/halo75_v2/iso:via`

Add or remove boards by editing the build commands in the relevant pipeline
files.

## One-time setup

### 1. Enable the repo in Woodpecker
In the Woodpecker UI: **Repositories -> +** and add this Forgejo repo.

### 2. Create the access token (Forgejo)
Forgejo -> **Settings -> Applications -> Generate New Token** with scope
`write:repository` (read+write repo). Copy the token.

### 3. Add the Woodpecker secret
In the repo's Woodpecker page: **Settings -> Secrets -> Add secret**

- **Name:** `forgejo_token`
- **Value:** the token from step 2

The secret is consumed by the `publish` steps as `FORGEJO_TOKEN`.
`CI_FORGE_URL`, `CI_REPO` and `CI_COMMIT_SHA` are injected by Woodpecker
automatically, so the Forgejo API URL is auto-detected.

### 4. Create the nightly cron
In the repo's Woodpecker page: **Settings -> Crons -> Add cron**

- **Name:** `nightly`   (must match `cron: nightly` in `nightly.yaml`)
- **Branch:** `master`  (or your default branch)
- **Schedule:** e.g. `0 3 * * *` (03:00 daily) or `@daily`

## Usage

### Test a build manually
In the repo's Woodpecker page click **+ / Run pipeline** (manual event) on a
branch. `build.yaml` compiles all targets and publishes nothing, so it's safe
to run as often as you like. It also runs automatically on pull requests.

### Cut a release
```sh
git tag v1.0.0
git push origin v1.0.0
```
`release.yaml` runs and a Forgejo release `v1.0.0` appears with the `.bin`
files and a `SHA256SUMS` file attached.

### Nightly
Runs automatically on the cron schedule. Each run publishes a pre-release named
`nightly-YYYY-MM-DD`, using the UTC date from the build container. Re-running the
nightly cron on the same date replaces that date's release, but it no longer
deletes or recreates a rolling `nightly` git tag.

## Notes

- Build image: `qmkfm/qmk_cli` (ships the ARM toolchain). The Halo75 V2 uses an
  STM32F072, so the artifacts are `.bin` files.
- Publishing uses the Forgejo/Gitea release API directly via `curl` + `jq`
  from the Woodpecker pipeline - no extra plugin required.
- To build without publishing (pure compile check), remove the `publish*` step
  from the relevant pipeline file.
