# Lincoln runtime FBX visual with moving wheels

This is a source upgrade for the CARLA 0.9.16 Linux runtime FBX prototype that
is already working in your build. At spawn it reads `linconkn.fbx` directly, separates
the rigidly weighted Root / FL / FR / RL / RR meshes, and creates a body and four
independent wheel visuals. A component follows the stock 2017 Lincoln's animated
wheel bones, including steering, rotation and suspension travel. The supplied FBX's
Assimp axes were measured and are converted to X forward, Y right, Z up.

**Status:** the FBX's geometry, axes, bone grouping and weights were inspected locally
with Blender and Assimp. The C++ has not been compiled in UE 4.26 or tested in CARLA;
wheel direction, handedness, alignment and runtime behavior still need your test.
It assumes the donor has bones `Wheel_Front_Left`, `Wheel_Front_Right`,
`Wheel_Rear_Left`, `Wheel_Rear_Right`; if not, it leaves the original Lincoln visible.

This is an animated visual rig, not a new Unreal skeletal mesh or a new CARLA
blueprint. It uses the existing Lincoln's collision, bounding box, wheel physics,
animation and handling; the FBX does not define physics. It does not load textures,
materials, lights, soft skinning or arbitrary rigs. The original CARLA light meshes
may still be visible. Wheel motion depends on the donor animation Blueprint updating.
The current FBX was found to be rigidly weighted (one bone per mesh part); mixed-bone
meshes are rejected. The entire vehicle uses the default material.

## Installation

1. Stop the running server. Extract this ZIP, then run the installer from the
   extracted directory:

   ```bash
   python3 install.py ~/carla-source
   ```

   It requires the **existing working FBX prototype and dispatcher hook**. It backs
   up any overwritten files under `~/carla-source/runtime_fbx_backup_<timestamp>/`.
   It does not change your FBX or the dispatcher, and does not install Assimp.

2. Recompile and package your modified server:

   ```bash
   source ~/carla-build-venv/bin/activate
   cd ~/carla-source
   make package
   ```

   This C++ change requires a new server build. It does **not** require cooking or
   importing the FBX. The build might take substantial memory and disk space; your
   earlier system had 48 GB free and no swap, so check `df -h ~` and `free -h` first.
   If build fails, keep the first actual error and report it. The old package remains
   usable unless the packaging process replaces its files.

3. Find the package produced by the build; do not assume the old directory is new:

   ```bash
   find ~/carla-source/Dist -name CarlaUE4-Linux-Shipping -printf '%TY-%Tm-%Td %TH:%TM %p\n' | sort -r
   ```

   The corresponding `CarlaUE4.sh` is in that binary's `LinuxNoEditor` directory.
   Set `CARLA_SH` to the chosen path, for example:

   ```bash
   CARLA_SH="$HOME/carla-source/Dist/CARLA_Shipping_294096eb1-dirty/LinuxNoEditor/CarlaUE4.sh"
   test -f "$CARLA_SH" && echo "Using $CARLA_SH"
   ```

   CARLA found Assimp by looking in this package's binary directory. If a newly
   created package doesn't have the symlink, create it (skip if already present):

   ```bash
   CARLA_BIN="$(dirname "$CARLA_SH")/CarlaUE4/Binaries/Linux"
   if [ ! -e "$CARLA_BIN/libassimp.so.5" ] && [ ! -L "$CARLA_BIN/libassimp.so.5" ]; then
     ln -s /lib/x86_64-linux-gnu/libassimp.so.5 "$CARLA_BIN/libassimp.so.5"
   fi
   ls -l "$CARLA_BIN/libassimp.so.5"
   ```

4. Start the new server:

   ```bash
   "$CARLA_SH" -RuntimeFbx="$HOME/carla-fbx-assets/linconkn.fbx" -nosound
   ```

   In another terminal, run:

   ```bash
   source ~/carla-build-venv/bin/activate
   cd ~/carla-source
   python3 PythonAPI/examples/manual_control.py \
     --filter 'vehicle.lincoln.mkz_2017' --generation All --rolename runtime_fbx
   ```

   Slowly drive forward, reverse and steer; inspect all four wheels. The visual
   should appear as a gray Lincoln with moving wheels. If the stock car appears,
   test that you launched the newly built server and that its Assimp symlink and
   FBX path exist. Shipping builds may suppress logs.

## Alignment controls

The measured FBX axes are mapped to CARLA's X forward, Y right, Z up. If needed,
restart the server and append one or more options:

```text
-RuntimeFbxYaw=180       Rotate the entire visual, degrees
-RuntimeFbxScale=1.0     Multiply automatically fitted model size
-RuntimeFbxOffsetX=0     Forward/backward offset, Unreal units (cm)
-RuntimeFbxOffsetY=0     Right/left offset, cm
-RuntimeFbxOffsetZ=0     Up/down offset, cm
```

For example, add `-RuntimeFbxOffsetZ=10` to raise the visual 10 cm. These options
adjust the visual only; they do not change the donor physics. There is no general
FBX skeletal animation support in this version.

## Roll back

Stop the server, copy `RuntimeFbxVisual.cpp` from the backup folder back to
`~/carla-source/Unreal/CarlaUE4/Plugins/Carla/Source/Carla/Vehicle/`, then remove
the two `RuntimeFbxRigComponent` files if they did not exist before installation,
and rebuild. If they existed before, restore their backed-up copies instead.

## Diagnostic

`tools/inspect_assimp.py` is an optional diagnostic for the supplied FBX on Linux
using `libassimp.so.5` and NumPy. It prints the Assimp mesh bone names and bounds.
It does not change the FBX and isn't needed to run CARLA.
