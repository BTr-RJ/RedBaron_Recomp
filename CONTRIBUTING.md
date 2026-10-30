# Contributing

Open a focused issue before substantial changes. Include version, hardware/driver, mode, reproduction steps, expected/actual result and a redacted log. Never upload original game/DLC files, generated C++, extracted metadata/icons, saves, credentials or tokens.

Keep launcher text and public documentation in English. Preserve settings and saves. SDK changes should be reproducible patches against the pinned base.

Build affected targets and run relevant CTest/launcher self-tests. State what was tested and what requires gameplay validation. Run `python scripts/audit_repository.py --public` before pushing. See [Building](docs/BUILDING.md).
