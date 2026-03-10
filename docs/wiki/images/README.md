# Metasiberia Wiki Images

This directory is the central image registry for the wiki.

## Recommended workflow

1. Find the target wiki page in this file.
2. Use the exact folder and filename listed for the image slot.
3. Drop the PNG into that folder without renaming the slot.
4. If an image already exists, replace it in place with a better version.
5. Re-run `scripts/publish_wiki_to_github.ps1` after updating images.

## Why this is better

- Page markdown stays stable because image paths do not keep changing.
- You can add images later without editing the page again.
- Missing and completed image slots are visible in one place.
- The same structure can scale to the full 44-page wiki.

## Important rule

More images do help the wiki, but only when each image explains a step, a result, or an error state.
Do not add near-duplicate screenshots just to increase count.

## Current image map

### Home

- File: `images/wiki_main_image.png`
- Status: present

### 01 Install Windows

- Folder: `images/01-install-windows/`
- `hero.png` - pending
- `step-release-page.png` - present
- `step-installer.png` - pending
- `result-installed.png` - pending
- `error-antivirus.png` - pending

### 02 Registration and Login

- Folder: `images/02-registration-and-login/`
- `hero.png` - pending
- `step-signup-form.png` - present
- `step-terms-checkbox.png` - present
- `result-logged-in.png` - pending
- `error-invalid-credentials.png` - pending

### 03 First Launch and Connection

- Folder: `images/03-first-launch-and-connection/`
- `hero.png` - present
- `step-connect.png` - present
- `step-world-loaded.png` - present
- `result-ready.png` - present
- `error-connection.png` - pending

## Naming convention for new pages

- Create one folder per page slug:
  - `images/<page-slug>/`
- Prefer descriptive slot names over generic numbering:
  - `hero.png`
  - `step-release-page.png`
  - `step-signup-form.png`
  - `result-ready.png`
  - `error-connection.png`

## Next pages to prepare

- `04-client-ui-overview`
- `05-movement-and-camera-modes`
- `06-graphics-audio-mic-webcam-settings`
- `07-troubleshooting-startup-and-login`
- `08-faq-quick-answers`
