#!/usr/bin/env python3
"""Install the wheel-aware visual loader into an existing working prototype."""
from datetime import datetime
from pathlib import Path
import argparse
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("carla_source", type=Path, help="CARLA source root")
    args = parser.parse_args()
    root = args.carla_source.expanduser().resolve()
    plugin = root / "Unreal/CarlaUE4/Plugins/Carla/Source/Carla"
    vehicle = plugin / "Vehicle"
    dispatcher = plugin / "Actor/ActorDispatcher.cpp"
    existing = vehicle / "RuntimeFbxVisual.cpp"
    if not dispatcher.is_file() or not existing.is_file():
        parser.error("existing runtime FBX prototype was not found in this CARLA source tree")
    if "TryAttachRuntimeFbxVisual(" not in dispatcher.read_text():
        parser.error("ActorDispatcher.cpp does not contain the existing runtime FBX hook")
    source = Path(__file__).resolve().parent / "source"
    files = ["RuntimeFbxVisual.cpp", "RuntimeFbxRigComponent.h", "RuntimeFbxRigComponent.cpp"]
    for name in files:
        if not (source / name).is_file():
            parser.error(f"bundle is missing {name}")
    backup = root / ("runtime_fbx_backup_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f"))
    backup.mkdir()
    for name in files:
        dst = vehicle / name
        if dst.exists():
            shutil.copy2(dst, backup / name)
        shutil.copy2(source / name, dst)
        print("Installed", dst)
    print("Backups:", backup)
    print("Next: rebuild CARLA; see README.md")


if __name__ == "__main__":
    main()
