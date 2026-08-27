#!/usr/bin/python3
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import plistlib
import re
import sys

FIELDS = ("app_name", "bundle_id", "executable_name", "url_scheme")
SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._-]*$")
BUNDLE_ID = re.compile(r"^[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z0-9][A-Za-z0-9-]*){2,}$")
URL_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*$")
SOURCE_FILES = (
    "makefile",
    "resources/macos/Info.plist",
    "scripts/package_macos_app.sh",
    "scripts/manage_macos_app.sh",
    "scripts/verify_macos_app.sh",
    "src/main.cpp",
    "src/menubar.mm",
    "src/native_extras.mm",
    "src/preload.cpp",
    "src/settings.cpp",
    "src/ws_socket.mm",
    "src/api/config.cpp",
    "src/api/disk_cache.cpp",
    "src/api/token_store.cpp",
    "src/ecs/main_pane_system.h",
    "src/ui/slash_commands.h",
    "src/util/spotlight_catalog.h",
)
FORBIDDEN = (
    "io.github.gabeochoa.hanabi",
    "Hanabi.app",
    "output/Hanabi.app",
    "Contents/MacOS/hanabi",
    '"hanabi://',
    '@"hanabi.',
    'Preload::get().init("hanabi")',
    'files::init("hanabi"',
    '/ "hanabi" /',
    '"hanabi.ws"',
    '"hanabi can\'t send',
    '"hanabi has no',
)


def load_config(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("branding config must be a JSON object")
    missing = sorted(set(FIELDS) - set(data))
    extra = sorted(set(data) - set(FIELDS))
    if missing:
        raise ValueError("missing branding fields: " + ", ".join(missing))
    if extra:
        raise ValueError("unknown branding fields: " + ", ".join(extra))
    return validate(data)


def validate(values):
    result = {field: values[field] for field in FIELDS}
    for field, value in result.items():
        if not isinstance(value, str):
            raise ValueError(f"{field} must be a string")
        if not value or value != value.strip():
            raise ValueError(f"{field} must be non-empty with no surrounding whitespace")
        if len(value) > 128:
            raise ValueError(f"{field} is too long")
    for field in ("app_name", "executable_name"):
        value = result[field]
        if not SAFE_NAME.fullmatch(value):
            raise ValueError(f"{field} must use only ASCII letters, digits, spaces, dot, underscore, or hyphen")
        if value in (".", "..") or value.endswith(".app"):
            raise ValueError(f"{field} is not a safe app path component")
    if not BUNDLE_ID.fullmatch(result["bundle_id"]):
        raise ValueError("bundle_id must be a reverse-DNS identifier with at least three components")
    if not URL_SCHEME.fullmatch(result["url_scheme"]):
        raise ValueError("url_scheme must match RFC 3986 scheme grammar")
    return result


def resolved(config, args):
    values = dict(config)
    for field in FIELDS:
        value = getattr(args, field, None)
        if value is not None:
            values[field] = value
    return validate(values)


def derived(values):
    bundle_id = values["bundle_id"]
    return {
        **values,
        "storage_name": values["url_scheme"].lower(),
        "url_type_name": bundle_id + ".thread",
        "notification_prefix": bundle_id + ".notification.",
        "spotlight_domain": bundle_id + ".threads",
        "spotlight_identifier_prefix": bundle_id + ".thread:",
        "spotlight_manifest_key": bundle_id + ".spotlight-identifiers-v1",
        "websocket_queue_label": bundle_id + ".ws",
    }


def identity_key(values):
    body = json.dumps(values, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(body).hexdigest()[:16]


def c_string(value):
    return json.dumps(value, ensure_ascii=True)


def render_header(values):
    values = derived(values)
    constants = (
        ("kAppName", "app_name"),
        ("kBundleIdentifier", "bundle_id"),
        ("kExecutableName", "executable_name"),
        ("kUrlScheme", "url_scheme"),
        ("kStorageName", "storage_name"),
        ("kUrlTypeName", "url_type_name"),
        ("kNotificationPrefix", "notification_prefix"),
        ("kSpotlightDomain", "spotlight_domain"),
        ("kSpotlightIdentifierPrefix", "spotlight_identifier_prefix"),
        ("kSpotlightManifestKey", "spotlight_manifest_key"),
        ("kWebSocketQueueLabel", "websocket_queue_label"),
    )
    lines = ["#pragma once", "", "namespace product_branding {"]
    lines.extend(
        f"inline constexpr char {name}[] = {c_string(values[field])};"
        for name, field in constants
    )
    lines.extend(("}", ""))
    return "\n".join(lines)


def render_plist(values, template_path):
    values = derived(values)
    replacements = {
        "@APP_NAME@": values["app_name"],
        "@EXECUTABLE_NAME@": values["executable_name"],
        "@BUNDLE_ID@": values["bundle_id"],
        "@URL_TYPE_NAME@": values["url_type_name"],
        "@URL_SCHEME@": values["url_scheme"],
        "@COPYRIGHT@": values["app_name"] + " contributors",
    }
    text = Path(template_path).read_text(encoding="utf-8")
    for token, value in replacements.items():
        if text.count(token) == 0:
            raise ValueError(f"Info.plist template is missing {token}")
        text = text.replace(token, html.escape(value, quote=False))
    if re.search(r"@[A-Z_]+@", text):
        raise ValueError("Info.plist template has an unresolved branding token")
    plistlib.loads(text.encode())
    return text


def write_if_changed(path, content):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    temporary = path.with_name(path.name + f".tmp.{os.getpid()}")
    temporary.write_text(content, encoding="utf-8")
    os.replace(temporary, path)


def generate(values, template_path, output_dir):
    output_dir = Path(output_dir)
    write_if_changed(output_dir / "branding.h", render_header(values))
    write_if_changed(output_dir / "Info.plist", render_plist(values, template_path))


def check_sources(root):
    failures = []
    root = Path(root)
    for relative in SOURCE_FILES:
        text = (root / relative).read_text(encoding="utf-8")
        for literal in FORBIDDEN:
            if literal in text:
                failures.append(f"{relative}: duplicated identity literal {literal!r}")
    if failures:
        raise ValueError("\n".join(failures))


def add_overrides(parser):
    for field in FIELDS:
        parser.add_argument("--" + field.replace("_", "-"), dest=field)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="resources/macos/branding.json")
    subparsers = parser.add_subparsers(dest="command", required=True)
    field_parser = subparsers.add_parser("field")
    field_parser.add_argument("name", choices=FIELDS)
    key_parser = subparsers.add_parser("key")
    add_overrides(key_parser)
    generate_parser = subparsers.add_parser("generate")
    add_overrides(generate_parser)
    generate_parser.add_argument("--template", required=True)
    generate_parser.add_argument("--output-dir", required=True)
    check_parser = subparsers.add_parser("check")
    add_overrides(check_parser)
    check_parser.add_argument("--template", required=True)
    check_parser.add_argument("--output-dir", required=True)
    check_parser.add_argument("--root", default=".")
    args = parser.parse_args()
    try:
        config = load_config(args.config)
        if args.command == "field":
            print(config[args.name])
            return
        values = resolved(config, args)
        if args.command == "key":
            print(identity_key(values))
            return
        if args.command == "generate":
            generate(values, args.template, args.output_dir)
            return
        generate(values, args.template, args.output_dir)
        expected_header = render_header(values)
        expected_plist = render_plist(values, args.template)
        output_dir = Path(args.output_dir)
        if (output_dir / "branding.h").read_text(encoding="utf-8") != expected_header:
            raise ValueError("generated branding.h does not match resolved branding")
        if (output_dir / "Info.plist").read_text(encoding="utf-8") != expected_plist:
            raise ValueError("generated Info.plist does not match resolved branding")
        check_sources(args.root)
        print("branding source check ok")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"branding: {error}", file=sys.stderr)
        raise SystemExit(2)


if __name__ == "__main__":
    main()
