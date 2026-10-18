# Repository maintenance tools

Audience: contributors, agents and CI authors. These tracked tools are shared between local workflows and GitHub Actions; CI-only dispatch, cache orchestration and upload entrypoints remain in `.github/scripts/`. Checkout-local probes and personal scripts remain in `.local-tools/`.

| Tool | Purpose |
| --- | --- |
| `release_version.py` | Resolve and temporarily inject the distribution revision |
| `release_notes.py` | Validate release provenance, render the Ayw changelog and compose the marked CI footer |
| `update_release_metadata.py` | Validate packages, generate/merge metadata and render rolling download links |
| `generate_changelog.py` | Generate the Ayw static changelog page under out/tmp/site/changelog |
| `out_layout.py` | Resolve output paths, stage retained programs and capture command logs |
| `clean_out.py` | Preview and apply selected cleanup categories in the documented layout |

Behavioral contracts live in the [out layout](../../docs/agents/out-layout.md), [release documents](../../docs/releases/README.md) and [updater documentation](../../docs/development/aywgram-updater.md); skills link to these same sources. Run tools from the repository root with `python tools/maintenance/<tool>.py --help`.

Run shared tool tests with `python -m unittest discover -s tools/maintenance/tests`, and CI entrypoint tests with `python -m unittest discover -s .github/scripts`. These do not compile the application or contact production release services. Destructive cleanup tests use isolated temporary output trees.
