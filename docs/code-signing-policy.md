# Code signing policy

**Status: application stage; SignPath Foundation approval has not been granted.** The published 1.0.0.4 official-data test installer is unsigned. Signing will be enabled only for a scope accepted by SignPath Foundation.

## Requested service

The requested attribution, applicable after acceptance, is: Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

## Project and data status

[Coral Pinyin](https://coralime.cn/) is maintained by the individual developer [Coral-IME](https://github.com/Coral-IME). Project-owned application source, build scripts and public demonstration fixtures use MPL-2.0. Third-party components retain their respective licenses; see [LICENSING.md](../LICENSING.md) and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

The official-data installer currently includes dictionaries and compiled models from a separate private repository under non-open terms. The maintainer plans to publish the dictionaries, models and corpora after internal debugging; a date and data license have not been committed. This application requests an eligibility assessment with that limitation disclosed. Signing of this package requires eligibility confirmation and any data publication required by SignPath. This policy does not change existing data licenses.

## Responsibilities

| Role | Maintainer |
| --- | --- |
| Author / committer | [Coral-IME](https://github.com/Coral-IME) |
| Reviewer of outside contributions | [Coral-IME](https://github.com/Coral-IME) |
| Proposed signing approver | [Coral-IME](https://github.com/Coral-IME) |

Outside contributions require maintainer review. Every release requires manual signing approval. Repository and signing accounts used for signing must use multi-factor authentication; the SignPath account must be configured before signing is enabled.

## Build and signing scope

Windows artifacts are built using the repository's [GitHub Actions workflows](../.github/workflows). Requests must bind artifacts to the reviewed source commit and build, enforce consistent project/product metadata and version, and preserve third-party notices. Only project-owned executables, libraries and the generated installer may be signed under the project's configuration; upstream binaries must not be re-signed as project-owned files. Installer reconstruction and checksums are verified during packaging. Final signed files require new SHA-256 checksums.

Signing credentials and private keys must not be committed to repositories or distributed with the application. The current workflows do not invoke SignPath.

## Privacy

See the [privacy policy](https://coralime.cn/privacy-policy/coral-pinyin/privacy-policy/). Normal typing and user learning are processed locally. Update checks and optional user-triggered cloud translation are described in that policy; the application is not represented as entirely offline. The published 1.0.0.4 GitHub test build has automatic updates disabled.
