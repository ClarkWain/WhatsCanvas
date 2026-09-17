"""MkDocs hook: stage repository ``images/`` into the built site.

The published docs live in ``doc/public/`` (``docs_dir``), but the visual assets
(comparison renders, the architecture diagram, game screenshots) live in the
repository-root ``images/`` folder so they can be shared with the README. This
hook adds those image files to the MkDocs file collection under the same
``images/...`` path, so markdown references like ``images/aa/quality_showcase.png``
resolve on the site for both ``mkdocs serve`` and ``mkdocs build`` — without
duplicating the binaries into ``doc/``.
"""

from __future__ import annotations

from pathlib import Path

from mkdocs.structure.files import File

_IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg"}


def on_files(files, config):
    root = Path(config["config_file_path"]).parent
    images_dir = root / "images"
    if not images_dir.is_dir():
        return files

    for path in images_dir.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in _IMAGE_SUFFIXES:
            continue
        relative = path.relative_to(root).as_posix()  # e.g. images/aa/quality_showcase.png
        files.append(
            File(
                relative,
                str(root),
                config["site_dir"],
                config["use_directory_urls"],
            )
        )
    return files
