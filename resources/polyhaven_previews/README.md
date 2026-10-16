# Poly Haven material previews

This directory contains the 868 texture previews in `catalog.json`, downloaded
on 2026-10-14 from the `thumbnail_url` recorded for each asset. The WebP filename
is the catalogue asset ID. Original authors and asset metadata are retained in
the catalogue. Source: https://polyhaven.com/textures

These are small browser previews, not the full PBR texture maps. The client uses
them when a preview is absent from its user cache, and downloads previews for
new assets in the background. The bundled catalogue also allows a fresh
installation to display the material browser while the API is unavailable.

The normal resource copy includes this directory under
`data/resources/polyhaven_previews` in the client distribution.

Poly Haven asset licensing and website terms: https://polyhaven.com/license
