# Pinned dependencies

| Dependency | Path | Version | Commit |
|---|---|---|---|
| Tracktion Engine | `external/tracktion_engine` | v3.2.0 | `0a5f4e6a5f53d09c89b414a44386a12df7fa1ec6` |
| JUCE | `external/JUCE` | 8.0.6 + 19 commits (between 8.0.6 and 8.0.7) | `19edd538429c93d277bf95b55aaa7e3eb545f951` |
| Catch2 | `external/Catch2` | v3.16.0 | `317ac1ed4c0bb6e6b91eafc817e05c488feffcb3` |
| Rubber Band | `external/rubberband` | v4.0.0 | `1d95888bec3ae0a17c0c4af791810d5a63f6bc35` |
| Signalsmith Stretch | `external/signalsmith-stretch` | 1.4.0 | `a670068d9aeb64913331d5cc29337b19a457a7df` |
| Signalsmith Linear (needed by Stretch) | `external/signalsmith-linear` | 0.6.4 | `de55e6a50ffcf6f8f43f649692d94691c7025151` |

JUCE is pinned to the commit that Tracktion Engine v3.2.0 itself pins in `modules/juce`, not to the newest 8.x tag (8.0.15). Reason: that is the combination Tracktion tested. Moving to a newer JUCE is a separate, deliberate step.

Rubber Band is compiled into the Tracktion module through its single-file build (GPL, fine under the AGPLv3 plan). Signalsmith Stretch and Linear are header-only (MIT); only the M0 benchmark uses them so far.

Not added yet: r8brain-free-src, libmp3lame (added by the tasks that need them).
