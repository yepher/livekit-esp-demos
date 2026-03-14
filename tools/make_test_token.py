#!/usr/bin/env python3
"""Create a LiveKit test token and print token + widget URL.

Loads config from ./env in the current directory, or from environment variables
(env vars override ./env). See: https://cloud.livekit.io/projects/p_/settings/keys
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from urllib.parse import urlencode

# ./env in current working directory; process env vars override file values
ENV_PATH = Path.cwd() / "env"
MEET_JOIN_URL_BASE = "https://meet.livekit.io/custom"


def load_env(path: Path) -> dict[str, str]:
    """Parse key=value from env file; strip comments and empty lines."""
    env: dict[str, str] = {}
    if not path.exists():
        return env
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                key, _, value = line.partition("=")
                env[key.strip()] = value.strip()
    return env


def main() -> None:
    env = load_env(ENV_PATH)
    # Environment variables override values from ./env
    for key in ("LIVEKIT_API_KEY", "LIVEKIT_API_SECRET", "LIVEKIT_URL", "LIVEKIT_ROOM"):
        if key in os.environ:
            env[key] = os.environ[key]

    api_key = env.get("LIVEKIT_API_KEY")
    api_secret = env.get("LIVEKIT_API_SECRET")
    livekit_url = env.get("LIVEKIT_URL")
    room = env.get("LIVEKIT_ROOM", "esp32Room")

    missing = [k for k, v in [
        ("LIVEKIT_API_KEY", api_key),
        ("LIVEKIT_API_SECRET", api_secret),
        ("LIVEKIT_URL", livekit_url),
        ("LIVEKIT_ROOM", room),
    ] if not v]
    if missing:
        print("Error: LiveKit credentials are missing.", file=sys.stderr)
        print(f"  Missing: {', '.join(missing)}", file=sys.stderr)
        print(file=sys.stderr)
        print("Set them in ./env (in this directory) or as environment variables.", file=sys.stderr)
        print("Get your API key, secret, and URL from LiveKit Cloud:", file=sys.stderr)
        print("  https://cloud.livekit.io/projects/p_/settings/keys", file=sys.stderr)
        print(file=sys.stderr)
        print("Then create an ./env file with:", file=sys.stderr)
        print("  LIVEKIT_API_KEY=...", file=sys.stderr)
        print("  LIVEKIT_API_SECRET=...", file=sys.stderr)
        print("  LIVEKIT_URL=wss://your-project.livekit.cloud", file=sys.stderr)
        print("  LIVEKIT_ROOM=esp32Room   (optional, this is the default)", file=sys.stderr)
        sys.exit(1)

    try:
        from livekit import api
    except ImportError:
        print("Error: install livekit-api, e.g. pip install livekit-api", file=sys.stderr)
        sys.exit(1)

    video_grants = api.VideoGrants(
        room_join=True,
        room=room,
        can_publish=True,
        can_subscribe=True,
    )

    # ESP32 token (for the device to join the room)
    esp32_token = (
        api.AccessToken(api_key, api_secret)
        .with_identity("ESP32")
        .with_grants(video_grants)
        .to_jwt()
    )
    print("ESP32 token (put this in the ESP32 code):")
    print(esp32_token)
    print()

    # User token (for joining the meet with the agent)
    user_token = (
        api.AccessToken(api_key, api_secret)
        .with_identity("User")
        .with_grants(video_grants)
        .to_jwt()
    )
    print("User token:")
    print(user_token)
    print()

    # URL for the user to open and join the room directly
    params = {"liveKitUrl": livekit_url, "token": user_token}
    join_url = f"{MEET_JOIN_URL_BASE}?{urlencode(params)}"
    print("User join URL (open in browser to join the room):")
    print(join_url)
    print()

    # sdkconfig.defaults snippet: copy/paste into 01-custom-hardware-quickstart/code/sdkconfig.defaults
    print("--- sdkconfig.defaults (copy into code/sdkconfig.defaults) ---")
    print("# CONFIG_LK_EXAMPLE_USE_SANDBOX=y")
    print("# CONFIG_LK_EXAMPLE_SANDBOX_ID=\"your-sandbox-id\"")
    print("CONFIG_LK_EXAMPLE_USE_PREGENERATED=y")
    print(f"CONFIG_LK_EXAMPLE_SERVER_URL=\"{livekit_url}\"")
    print(f"CONFIG_LK_EXAMPLE_TOKEN=\"{esp32_token}\"")


if __name__ == "__main__":
    main()
