# Pinned dependencies

| Dependency | Path | Version | Commit |
|---|---|---|---|
| Tracktion Engine | `external/tracktion_engine` | v3.2.0 | `0a5f4e6a5f53d09c89b414a44386a12df7fa1ec6` |
| JUCE | `external/JUCE` | 8.0.6 + 19 commits (between 8.0.6 and 8.0.7) | `19edd538429c93d277bf95b55aaa7e3eb545f951` |
| Catch2 | `external/Catch2` | v3.16.0 | `317ac1ed4c0bb6e6b91eafc817e05c488feffcb3` |

JUCE is pinned to the commit that Tracktion Engine v3.2.0 itself pins in `modules/juce`, not to the newest 8.x tag (8.0.15). Reason: that is the combination Tracktion tested. Moving to a newer JUCE is a separate, deliberate step.

Not added yet: Rubber Band, Signalsmith Stretch, r8brain-free-src, libmp3lame (added by the tasks that need them).
