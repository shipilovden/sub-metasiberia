# Metasiberia Wiki (Docs-as-Code)

This directory stores Wiki content in the main repository.

## Goal
- Keep user documentation versioned with code changes.
- Review docs in pull requests.
- Publish the same content to GitHub Wiki pages.

## Current Scope
- Single-page beginner onboarding (Home).
- Detailed expansion plan: `WIKI-PAGES-PLAN-RU.md`.
- Capability audit drafts:
  - `TEMP-METASIBERIA-CAPABILITIES-PLAN.md`
  - `TEMP-METASIBERIA-CAPABILITIES-PLAN-RU.md`

## File Layout
- `Home.md` - wiki landing page
- `_Sidebar.md` - navigation

## Editing Rules
- One topic per page.
- Keep steps short and explicit.
- Add troubleshooting at the end of each page.
- Keep only published pages in `_Sidebar.md`. Planned pages stay in `WIKI-PAGES-PLAN-RU.md` until they are drafted.
- `shields.io` badges are allowed on `Home.md` for status, entry points, releases, and community links. Avoid badge overload on step-by-step tutorial pages.
- For planned full wiki structure and mandatory images per page, use `WIKI-PAGES-PLAN-RU.md` as source of truth.
