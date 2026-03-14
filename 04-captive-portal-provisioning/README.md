# 04 – Captive Portal Provisioning

Device onboarding without hardcoded credentials. Implements a captive portal flow for Wi-Fi, LiveKit URL, and token configuration so you never need to recompile for a new network.

## What You'll Learn

- Setting up a captive portal on the ESP32 for device onboarding
- Collecting Wi-Fi credentials, LiveKit URL, and token from a browser
- Persisting configuration to NVS so it survives reboots
- Building a deployment-ready config flow you can hand to non-developers

## Directory Structure

```
04-captive-portal-provisioning/
├── blog/
│   ├── post.md       # Blog post
│   └── res/          # Images and resources
└── code/             # Source code
```
