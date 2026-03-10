# Metasiberia Wiki (Docs-as-Code)

This directory stores Wiki content in the main repository.

## Goal
- Keep user documentation versioned with code changes.
- Review docs in pull requests.
- Publish the same content to GitHub Wiki pages.

## Current Scope
- Beginner onboarding pages:
  - `Home.md`
  - `01-Install-Windows.md`
  - `02-Registration-and-Login.md`
  - `03-First-Launch-and-Connection.md`
- Detailed expansion plan: `WIKI-PAGES-PLAN-RU.md`.
- Capability audit drafts:
  - `TEMP-METASIBERIA-CAPABILITIES-PLAN.md`
  - `TEMP-METASIBERIA-CAPABILITIES-PLAN-RU.md`

## File Layout
- `Home.md` - wiki landing page
- `_Sidebar.md` - navigation
- `_Footer.md` - footer navigation
- `images/README.md` - central image index and naming rules

## Publish
- Public GitHub Wiki is a separate git repository: `https://github.com/shipilovden/sub-metasiberia.wiki.git`
- Use `scripts/publish_wiki_to_github.ps1` to publish the supported subset from `docs/wiki/`.
- Published subset: `Home.md`, numbered published pages, `WIKI-PAGES-PLAN-RU.md`, `_Sidebar.md`, `_Footer.md`, and `images/`.

## Editing Rules
- One topic per page.
- Keep steps short and explicit.
- Add troubleshooting at the end of each page.
- Keep only published pages in `_Sidebar.md`. Planned pages stay in `WIKI-PAGES-PLAN-RU.md` until they are drafted.
- `shields.io` badges are allowed on `Home.md` for status, entry points, releases, and community links. Avoid badge overload on step-by-step tutorial pages.
- For planned full wiki structure and mandatory images per page, use `WIKI-PAGES-PLAN-RU.md` as source of truth.
