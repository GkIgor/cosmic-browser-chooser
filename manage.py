#!/usr/bin/python3
"""Install, inspect, and remove the browser chooser for the current user."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

DESKTOP_ID = "dev.local.BrowserChooser.desktop"
HANDLED_TYPES = (
    "x-scheme-handler/http",
    "x-scheme-handler/https",
    "text/html",
    "application/xhtml+xml",
)


PROJECT_DIR = Path(__file__).resolve().parent
DATA_HOME = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
CONFIG_HOME = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
COSMIC_MIMEAPPS = CONFIG_HOME / "cosmic-mimeapps.list"
APP_DIR = DATA_HOME / "browser-chooser"
INSTALLED_APP = APP_DIR / "browser-chooser"
INSTALLED_MANAGER = APP_DIR / "manage.py"
INSTALLED_TEMPLATE = APP_DIR / DESKTOP_ID
INSTALLED_DESKTOP = DATA_HOME / "applications" / DESKTOP_ID
BACKUP = CONFIG_HOME / "browser-chooser" / "previous-defaults.json"


def mime_default(content_type: str) -> str:
    result = subprocess.run(
        ["xdg-mime", "query", "default", content_type],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def entries_in_default_section(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    section = ""
    entries = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped
        elif section == "[Default Applications]" and "=" in line:
            key, value = line.split("=", 1)
            entries[key.strip()] = value.strip()
    return entries


def change_entry(path: Path, content_type: str, value: str | None) -> None:
    """Change one association while preserving the rest of the file."""
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True) if path.exists() else []
    section = ""
    found_section = False
    changed = False
    insert_at = None
    result = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if section == "[Default Applications]" and insert_at is None:
                insert_at = len(result)
            section = stripped
            found_section |= section == "[Default Applications]"
        if section == "[Default Applications]" and "=" in line:
            key = line.split("=", 1)[0].strip()
            if key == content_type:
                if value is not None and not changed:
                    result.append(f"{content_type}={value}\n")
                changed = True
                continue
        result.append(line)

    if not changed and value is not None:
        if not found_section:
            if result and not result[-1].endswith("\n"):
                result[-1] += "\n"
            result.extend(["[Default Applications]\n", f"{content_type}={value}\n"])
        else:
            index = insert_at if insert_at is not None else len(result)
            result.insert(index, f"{content_type}={value}\n")

    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".browser-chooser.tmp")
    temp.write_text("".join(result), encoding="utf-8")
    temp.replace(path)


def desktop_contents() -> str:
    source = (PROJECT_DIR / DESKTOP_ID).read_text(encoding="utf-8")
    if any(char.isspace() for char in str(INSTALLED_APP)):
        raise RuntimeError("O xdg-open desta versão não aceita caminhos de instalação com espaços.")
    exec_line = f"Exec={INSTALLED_APP} %u"
    return source.replace("Exec=browser-chooser %u", exec_line, 1)


def update_desktop_database() -> None:
    updater = shutil.which("update-desktop-database")
    if updater:
        subprocess.run([updater, str(INSTALLED_DESKTOP.parent)], check=True)


def install() -> None:
    source_binary = PROJECT_DIR / "browser-chooser"
    if (PROJECT_DIR / "chooser.c").is_file():
        subprocess.run(["make", "-C", str(PROJECT_DIR)], check=True)
    if not source_binary.is_file():
        raise RuntimeError("Executável ausente; compile com 'make' antes de instalar.")

    if BACKUP.exists():
        backup = json.loads(BACKUP.read_text(encoding="utf-8"))
        if backup.get("version") != 2:
            # Upgrade the backup created by the first installer, which used
            # xdg-mime and therefore wrote to mimeapps.list rather than COSMIC's file.
            backup = {
                "version": 2,
                "defaults": backup,
                "cosmic_file_existed": COSMIC_MIMEAPPS.exists(),
                "cosmic_entries": {
                    content_type: entries_in_default_section(COSMIC_MIMEAPPS).get(content_type)
                    for content_type in HANDLED_TYPES
                },
                "legacy_generic_cleanup": True,
            }
            BACKUP.write_text(json.dumps(backup, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    else:
        current = {content_type: mime_default(content_type) for content_type in HANDLED_TYPES}
        if DESKTOP_ID in current.values():
            raise RuntimeError("O seletor já é padrão, mas não há backup para restaurar depois.")
        entries = entries_in_default_section(COSMIC_MIMEAPPS)
        backup = {
            "version": 2,
            "defaults": current,
            "cosmic_file_existed": COSMIC_MIMEAPPS.exists(),
            "cosmic_entries": {content_type: entries.get(content_type) for content_type in HANDLED_TYPES},
            "legacy_generic_cleanup": False,
        }
        BACKUP.parent.mkdir(parents=True, exist_ok=True)
        BACKUP.write_text(json.dumps(backup, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    if backup.get("version") != 2 or set(backup.get("cosmic_entries", {})) != set(HANDLED_TYPES):
        raise RuntimeError(f"Backup existente incompleto: {BACKUP}")

    APP_DIR.mkdir(parents=True, exist_ok=True)
    INSTALLED_DESKTOP.parent.mkdir(parents=True, exist_ok=True)
    for source, destination in (
        (source_binary, INSTALLED_APP),
        (PROJECT_DIR / "manage.py", INSTALLED_MANAGER),
        (PROJECT_DIR / DESKTOP_ID, INSTALLED_TEMPLATE),
    ):
        if source.resolve() != destination.resolve():
            shutil.copy2(source, destination)
    INSTALLED_APP.chmod(0o755)
    INSTALLED_MANAGER.chmod(0o755)
    INSTALLED_DESKTOP.write_text(desktop_contents(), encoding="utf-8")
    update_desktop_database()

    for content_type in HANDLED_TYPES:
        change_entry(COSMIC_MIMEAPPS, content_type, DESKTOP_ID)
        actual = mime_default(content_type)
        if actual != DESKTOP_ID:
            raise RuntimeError(f"Não foi possível registrar {content_type}: padrão atual é {actual!r}")

    if backup.get("legacy_generic_cleanup"):
        generic = CONFIG_HOME / "mimeapps.list"
        if entries_in_default_section(generic).get("x-scheme-handler/http") == DESKTOP_ID:
            change_entry(generic, "x-scheme-handler/http", None)
        backup["legacy_generic_cleanup"] = False
        BACKUP.write_text(json.dumps(backup, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    # Remove the old Python launcher only after the compiled launcher is active.
    (APP_DIR / "chooser.py").unlink(missing_ok=True)
    print("Instalado. O seletor agora abrirá links e arquivos HTML.")


def uninstall() -> None:
    if not BACKUP.exists():
        raise RuntimeError(f"Backup ausente: {BACKUP}. Remoção cancelada para preservar seus padrões.")
    backup = json.loads(BACKUP.read_text(encoding="utf-8"))
    if backup.get("version") != 2:
        raise RuntimeError("Backup antigo; execute 'install' novamente antes de remover.")
    for content_type in HANDLED_TYPES:
        if mime_default(content_type) != DESKTOP_ID:
            continue  # The user changed this default after installing us.
        change_entry(COSMIC_MIMEAPPS, content_type, backup["cosmic_entries"].get(content_type))

    if not backup["cosmic_file_existed"] and COSMIC_MIMEAPPS.exists():
        remaining = COSMIC_MIMEAPPS.read_text(encoding="utf-8").strip()
        if remaining == "[Default Applications]":
            COSMIC_MIMEAPPS.unlink()

    INSTALLED_DESKTOP.unlink(missing_ok=True)
    update_desktop_database()
    still_active = [content_type for content_type in HANDLED_TYPES if mime_default(content_type) == DESKTOP_ID]
    if still_active:
        INSTALLED_DESKTOP.write_text(desktop_contents(), encoding="utf-8")
        update_desktop_database()
        raise RuntimeError(f"Ainda ativo para {', '.join(still_active)}; arquivos preservados.")

    INSTALLED_APP.unlink(missing_ok=True)
    (APP_DIR / "chooser.py").unlink(missing_ok=True)
    INSTALLED_MANAGER.unlink(missing_ok=True)
    INSTALLED_TEMPLATE.unlink(missing_ok=True)
    if APP_DIR.exists():
        try:
            APP_DIR.rmdir()
        except OSError:
            pass
    BACKUP.unlink()
    print("Removido. Os aplicativos padrão anteriores foram restaurados.")


def status() -> None:
    print(f"Aplicativo: {'instalado' if INSTALLED_APP.is_file() else 'não instalado'}")
    print(f"Entrada XDG: {'instalada' if INSTALLED_DESKTOP.is_file() else 'não instalada'}")
    print(f"Backup: {BACKUP if BACKUP.is_file() else 'ausente'}")
    for content_type in HANDLED_TYPES:
        print(f"{content_type}: {mime_default(content_type) or '(nenhum)'}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Gerencie o seletor de navegador")
    parser.add_argument("action", choices=("install", "uninstall", "status"))
    args = parser.parse_args()
    try:
        {"install": install, "uninstall": uninstall, "status": status}[args.action]()
    except (OSError, ValueError, subprocess.CalledProcessError, RuntimeError) as error:
        parser.exit(1, f"Erro: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
