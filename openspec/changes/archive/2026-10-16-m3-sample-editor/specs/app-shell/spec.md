# Spec Delta

## MODIFIED Requirements

### Requirement: Main view shows the arrangement
The main view area SHALL show the arrangement: the track headers on the left, the ruler and lanes on the right, and the clip inspector in a bottom panel. The "Drop audio here" hint is shown in the lanes while the project has no clips, with the text "Drag a sample from the browser onto the timeline to create a clip". The Session / Arrange toggle shows Arrange; Session is disabled until the Session view exists. While a sound is open in the sample editor, the main view area shows the sample editor in place of the arrangement and the clip inspector, until the user goes back; the Session / Arrange toggle then shows neither, and choosing Arrange goes back.

#### Scenario: Empty project
- **WHEN** the user starts the app
- **THEN** the main view shows one empty track, the ruler, the hint text and an empty bottom panel, and Session cannot be chosen

#### Scenario: Arrange from the toggle
- **WHEN** the sample editor is shown and the user clicks Arrange in the header toggle
- **THEN** the arrangement is shown as it was before the editor opened

#### Scenario: New project closes the editor
- **WHEN** the sample editor is shown and the user creates or opens another project
- **THEN** the main view shows that project's arrangement

### Requirement: Keyboard shortcuts follow focus
Key presses SHALL go to the most specific active scope: an open dialog first, then a focused text field, then the sample editor's pad keys (while the sample editor has focus), then the focused view, then global shortcuts. Global shortcuts (Space, Home, Enter, L, Mod+L, Mod+T, Mod+R, the File and Edit menu shortcuts) work from any view, but not while a text field or dialog takes the key. Holding a key down does not repeat transport actions.

#### Scenario: Space in a text field
- **WHEN** the user is renaming a track and presses Space
- **THEN** a space is typed into the name and playback does not start

#### Scenario: Space in the browser
- **WHEN** the browser list (not its search box) has focus and the user presses Space
- **THEN** playback starts or stops

#### Scenario: Pad key in the sample editor
- **WHEN** the sample editor has focus and the user presses L
- **THEN** looping is toggled, because L is not a pad key, while pressing A plays pad 5 instead of any other action

#### Scenario: Undo in the sample editor
- **WHEN** the sample editor has focus and the user presses Mod+Z
- **THEN** the last edit is undone and no pad plays
