#!/usr/bin/env python3
"""Verify the paired MkDocs sites required by Material's language switcher."""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


PROJECT_SOURCE_LINKS = {
    Path("project/index.md"): (
        "https://github.com/lusipad/plcopen/blob/main/STATUS.md",
        "https://github.com/lusipad/plcopen/blob/main/"
        "doc/design/core/architecture.md",
        "https://github.com/lusipad/plcopen/blob/main/"
        "doc/compliance/plcopen-conformance-audit.md",
        "https://github.com/lusipad/plcopen/blob/main/"
        "doc/compliance/known-boundaries.md",
        "https://github.com/lusipad/plcopen/blob/main/CONTRIBUTING.md",
        "https://github.com/lusipad/plcopen/blob/main/GOVERNANCE.md",
        "https://github.com/lusipad/plcopen/blob/main/SECURITY.md",
        "https://github.com/lusipad/plcopen/blob/main/CHANGELOG.md",
    ),
    Path("project/status.md"): (
        "https://github.com/lusipad/plcopen/blob/main/STATUS.md",
    ),
    Path("project/architecture.md"): (
        "https://github.com/lusipad/plcopen/blob/main/"
        "doc/design/core/architecture.md",
    ),
    Path("project/compliance.md"): (
        "https://github.com/lusipad/plcopen/blob/main/"
        "doc/compliance/plcopen-conformance-audit.md",
    ),
    Path("project/known-boundaries.md"): (
        "https://github.com/lusipad/plcopen/blob/main/"
        "doc/compliance/known-boundaries.md",
    ),
    Path("project/contributing.md"): (
        "https://github.com/lusipad/plcopen/blob/main/CONTRIBUTING.md",
    ),
    Path("project/governance.md"): (
        "https://github.com/lusipad/plcopen/blob/main/GOVERNANCE.md",
    ),
    Path("project/security.md"): (
        "https://github.com/lusipad/plcopen/blob/main/SECURITY.md",
    ),
    Path("project/changelog.md"): (
        "https://github.com/lusipad/plcopen/blob/main/CHANGELOG.md",
    ),
}

LEGACY_PROJECT_NAV_LINKS = {
    "https://github.com/lusipad/plcopen/tree/main/doc/compliance",
    "https://github.com/lusipad/plcopen/blob/main/"
    "doc/design/core/architecture.md",
    "https://github.com/lusipad/plcopen/blob/main/"
    "doc/compliance/known-boundaries.md",
    "https://github.com/lusipad/plcopen/blob/main/CONTRIBUTING.md",
    "https://github.com/lusipad/plcopen/blob/main/GOVERNANCE.md",
    "https://github.com/lusipad/plcopen/blob/main/SECURITY.md",
    "https://github.com/lusipad/plcopen/blob/main/CHANGELOG.md",
}


class PageMetadata(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.lang: str | None = None
        self.canonical: str | None = None
        self.link_alternates: set[tuple[str | None, str | None]] = set()
        self.selector_alternates: set[tuple[str | None, str | None]] = set()

    def handle_starttag(
        self, tag: str, attrs: list[tuple[str, str | None]]
    ) -> None:
        values = dict(attrs)
        if tag == "html":
            self.lang = values.get("lang")
        elif tag == "link" and values.get("rel") == "canonical":
            self.canonical = values.get("href")
        elif tag == "link" and values.get("rel") == "alternate":
            self.link_alternates.add(
                (values.get("hreflang"), values.get("href"))
            )
        elif tag == "a" and values.get("hreflang"):
            self.selector_alternates.add(
                (values.get("hreflang"), values.get("href"))
            )


def markdown_files(root: Path) -> set[Path]:
    return {path.relative_to(root) for path in root.rglob("*.md")}


def rendered_page(site: Path, prefix: Path, source: Path) -> Path:
    if source.name == "index.md":
        return site / prefix / source.parent / "index.html"
    return site / prefix / source.with_suffix("") / "index.html"


def url_suffix(source: Path) -> str:
    if source.name == "index.md":
        parent = source.parent.as_posix()
        return "" if parent == "." else f"{parent}/"
    return f"{source.with_suffix('').as_posix()}/"


def parse_page(path: Path) -> PageMetadata:
    page = PageMetadata()
    page.feed(path.read_text(encoding="utf-8"))
    return page


def sitemap_urls(path: Path) -> set[str]:
    root = ET.parse(path).getroot()
    namespace = {"s": "http://www.sitemaps.org/schemas/sitemap/0.9"}
    return {
        element.text
        for element in root.findall("s:url/s:loc", namespace)
        if element.text
    }


def verify_project_sources(root: Path, label: str) -> list[str]:
    errors: list[str] = []
    for source, canonical_links in PROJECT_SOURCE_LINKS.items():
        path = root / source
        if not path.is_file():
            errors.append(f"missing {label} project source: {source.as_posix()}")
            continue
        text = path.read_text(encoding="utf-8")
        errors.extend(
            f"{path}: missing canonical source link {canonical_link}"
            for canonical_link in canonical_links
            if canonical_link not in text
        )
    return errors


def verify_project_navigation(config: Path, label: str) -> list[str]:
    text = config.read_text(encoding="utf-8")
    errors = [
        f"{config}: {label} navigation missing {source.as_posix()}"
        for source in PROJECT_SOURCE_LINKS
        if source.as_posix() not in text
    ]
    errors.extend(
        f"{config}: {label} navigation still uses external project link {link}"
        for link in LEGACY_PROJECT_NAV_LINKS
        if link in text
    )
    return errors


def verify(
    site: Path,
    english_docs: Path,
    chinese_docs: Path,
    english_config: Path,
    chinese_config: Path,
    base_url: str,
) -> list[str]:
    errors: list[str] = []
    english_sources = markdown_files(english_docs)
    chinese_sources = markdown_files(chinese_docs)
    if english_sources != chinese_sources:
        for source in sorted(english_sources - chinese_sources):
            errors.append(f"missing Chinese source: {source.as_posix()}")
        for source in sorted(chinese_sources - english_sources):
            errors.append(f"missing English source: {source.as_posix()}")
        return errors

    errors.extend(verify_project_sources(english_docs, "English"))
    errors.extend(verify_project_sources(chinese_docs, "Chinese"))
    errors.extend(verify_project_navigation(english_config, "English"))
    errors.extend(verify_project_navigation(chinese_config, "Chinese"))

    base_url = base_url.rstrip("/") + "/"
    expected_alternates = {
        ("zh", "/plcopen/"),
        ("en", "/plcopen/en/"),
    }
    try:
        chinese_sitemap = sitemap_urls(site / "sitemap.xml")
        english_sitemap = sitemap_urls(site / "en" / "sitemap.xml")
    except (FileNotFoundError, ET.ParseError) as error:
        return [f"invalid sitemap: {error}"]

    for source in sorted(english_sources):
        suffix = url_suffix(source)
        pairs = (
            (
                "Chinese",
                rendered_page(site, Path(), source),
                "zh",
                f"{base_url}{suffix}",
                chinese_sitemap,
            ),
            (
                "English",
                rendered_page(site, Path("en"), source),
                "en",
                f"{base_url}en/{suffix}",
                english_sitemap,
            ),
        )
        for label, output, lang, canonical, sitemap in pairs:
            if not output.is_file():
                errors.append(f"missing {label} page: {output}")
                continue
            page = parse_page(output)
            if page.lang != lang:
                errors.append(
                    f"{output}: expected lang={lang!r}, got {page.lang!r}"
                )
            if page.canonical != canonical:
                errors.append(
                    f"{output}: expected canonical {canonical!r}, "
                    f"got {page.canonical!r}"
                )
            if not expected_alternates.issubset(page.link_alternates):
                errors.append(f"{output}: incomplete alternate link metadata")
            if not expected_alternates.issubset(page.selector_alternates):
                errors.append(f"{output}: incomplete language selector")
            if canonical not in sitemap:
                errors.append(f"{output}: canonical missing from sitemap")

    notebook = site / "en" / "notebooks" / "five-minute-digital-twin.ipynb"
    if not notebook.is_file():
        errors.append(f"missing shared English notebook: {notebook}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--site-dir", type=Path, default=Path("site"))
    parser.add_argument("--english-docs", type=Path, default=Path("docs"))
    parser.add_argument("--chinese-docs", type=Path, default=Path("docs.zh"))
    parser.add_argument(
        "--english-config", type=Path, default=Path("mkdocs.en.yml")
    )
    parser.add_argument(
        "--chinese-config", type=Path, default=Path("mkdocs.yml")
    )
    parser.add_argument(
        "--base-url", default="https://lusipad.com/plcopen/"
    )
    args = parser.parse_args()

    errors = verify(
        args.site_dir,
        args.english_docs,
        args.chinese_docs,
        args.english_config,
        args.chinese_config,
        args.base_url,
    )
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    count = len(markdown_files(args.english_docs))
    print(f"docs i18n verification passed: {count} paired pages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
