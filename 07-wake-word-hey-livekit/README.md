# 07 – Wake Word: "Hey LiveKit"

On-device wake word detection to trigger a LiveKit WebRTC session. Documents the journey from the stock "Hi ESP" model to a custom "Hey LiveKit" phrase using WakeNet9, including training data and model constraints.

## What You'll Learn

- How WakeNet9 works and what model architectures are available (conv1d vs heavier models)
- Training a custom wake phrase from "Hi ESP" to "Hey LiveKit"
- Training data requirements and constraints
- Integrating wake word detection with LiveKit session lifecycle
- How this relates to broader wake word work in livekit-wakeword

## Directory Structure

```
07-wake-word-hey-livekit/
├── blog/
│   ├── post.md       # Blog post
│   └── res/          # Images and resources
└── code/             # Source code
```
