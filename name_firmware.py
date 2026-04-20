import os
import re
import shutil

# --- LINTER FIX ---
class MockEnv:
    def get(self, key): return "."
    def AddPostAction(self, target, callback): pass
    def AddPreAction(self, target, callback): pass
    def subst(self, s): return s
    def Replace(self, **kwargs): pass

env = MockEnv()

try:
    from SCons.Script import Import  # type: ignore
    Import("env")  # type: ignore
except ImportError:
    pass
# ------------------


def extract_macro_value(content, macro_name):
    """
    Finds a #define and returns its value as a string.
    Handles quotes if present. Returns None if not found.
    """
    pattern = r"#define\s+" + re.escape(macro_name) + r'\s+"?([^"\s/]+)"?'
    match = re.search(pattern, content)
    if match:
        return match.group(1)
    return None


def is_macro_enabled(content, macro_name):
    """
    Returns True if the macro is defined and its value is NOT '0' or 'false'.
    """
    val = extract_macro_value(content, macro_name)
    if val and val != "0" and val.lower() != "false":
        return True
    return False


def get_firmware_info():
    """
    Scans header files to determine version, edition, and feature flags.
    """
    src_dir = env.get("PROJECT_SRC_DIR")

    config_file = os.path.join(src_dir, "tm_config.h")
    trace_file = os.path.join(src_dir, "tm_wk_trace.h")

    info = {
        "version": "0.0.0",
        "edition": "CE",
        "is_debug": False,
        "is_trace": False,
    }

    if os.path.exists(config_file):
        try:
            with open(config_file, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()

            v = extract_macro_value(content, "TM_VERSION")
            if v:
                info["version"] = v

            e = extract_macro_value(content, "TM_EDITION_SHORT")
            if e:
                info["edition"] = e

            info["is_debug"] = is_macro_enabled(content, "DEBUG")
        except Exception as e:
            print(f"Warning: Could not parse tm_config.h: {e}")

    if os.path.exists(trace_file):
        try:
            with open(trace_file, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
            info["is_trace"] = is_macro_enabled(content, "TM_WK_TRACE_SD")
        except Exception as e:
            print(f"Warning: Could not parse tm_wk_trace.h: {e}")

    return info


def build_custom_base_name():
    """
    Builds the custom firmware base name (no extension).
    """
    info = get_firmware_info()
    ver_safe = info["version"].replace(".", "-")

    base = f"TM{info['edition']}-{ver_safe}"
    if info["is_debug"]:
        base += "-debug"
    if info["is_trace"]:
        base += "-wk-trace-sd"

    return base


def _resolve_default_hex_path():
    """
    Resolve the .hex that PlatformIO actually built (usually firmware.hex).
    """
    p1 = env.subst("$BUILD_DIR/firmware.hex")
    if os.path.exists(p1):
        return p1
    return env.subst("$BUILD_DIR/${PROGNAME}.hex")


def ensure_custom_hex_exists():
    """
    Creates/updates a custom-named copy of the built hex in the same build folder.
    Returns the full path to the custom hex, or None on failure.
    """
    default_hex = _resolve_default_hex_path()
    if not os.path.exists(default_hex):
        print(f"Warning: Built hex not found (expected {default_hex})")
        return None

    build_dir = os.path.dirname(default_hex)
    custom_hex = os.path.join(build_dir, f"{build_custom_base_name()}.hex")

    try:
        if (not os.path.exists(custom_hex)) or (os.path.getmtime(custom_hex) < os.path.getmtime(default_hex)):
            shutil.copyfile(default_hex, custom_hex)
            print(f"Created custom firmware: {custom_hex}")
        else:
            print(f"Custom firmware already up-to-date: {custom_hex}")
        return custom_hex
    except Exception as e:
        print(f"Error creating custom firmware: {e}")
        return None


def post_build_create_custom_hex(*_args, **_kwargs):
    """
    SCons post-action: must return 0/None.
    """
    ensure_custom_hex_exists()
    return 0


def pre_upload_force_custom_hex(*_args, **_kwargs):
    """
    Pre-upload action: ensure the custom hex exists and force the custom uploader
    (TyTools tycmd) to upload THAT file by overriding the upload action command.
    """
    custom_hex = ensure_custom_hex_exists()
    if not custom_hex:
        return 0

    print(f"Upload will use: {custom_hex}")

    # Build the exact command we want to execute.
    # We do NOT rely on PlatformIO's default 'Uploading firmware.hex' message.
    tycmd = "tycmd"
    board = env.subst("$UPLOAD_PORT")  # often empty for TyTools; we keep your -B id below instead
    # Your log shows it uses -B 18009060; keep that stable.
    cmd = f'{tycmd} upload -B 18009060 --wait "{custom_hex}"'

    # Force both the command string AND the actual action to use it.
    env.Replace(UPLOADCMD=cmd)

    # Many PlatformIO platforms execute $UPLOADCMD via $UPLOADER/$UPLOAD_FLAGS,
    # but 'custom' can be special. Overriding UPLOADCMD is still the correct lever,
    # and printing it gives you hard evidence.
    print(f"UPLOADCMD (effective): {cmd}")

    return 0


# After build has produced a hex, create/update the custom-named copy.
env.AddPostAction("$BUILD_DIR/firmware.hex", post_build_create_custom_hex)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", post_build_create_custom_hex)

# Before upload, force the upload command to include the custom hex.
env.AddPreAction("upload", pre_upload_force_custom_hex)
