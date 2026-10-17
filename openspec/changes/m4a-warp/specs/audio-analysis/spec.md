# Spec Delta

## MODIFIED Requirements

### Requirement: Sounds are analysed in the background
Each sound SHALL be analysed for its hits and its tempo on a background job, when it is imported or recorded and, for sounds that have no analysis yet (projects from earlier versions, a deleted cache), when the project is opened. The jobs SHALL be queued so that the app stays responsive while analysis runs, and analysis SHALL NOT mark the project as changed or add an undo step. A sound whose analysis is still missing when it is opened in the sample editor is analysed then, as before.

#### Scenario: Import starts analysis
- **WHEN** the user imports a 4-bar drum loop and then opens it in the sample editor
- **THEN** its tempo and hits are shown without a further wait

#### Scenario: Drop without opening the editor
- **WHEN** the user imports a 4-bar loop, waits a moment and drags it onto a track without opening it in the sample editor
- **THEN** the clip is warped in Beats mode at the loop's tempo

#### Scenario: Older project
- **WHEN** a project from the previous version, or one whose cache folder was deleted, is opened
- **THEN** its sounds are analysed in the background, the rest of the app stays usable meanwhile, and the project is not marked as changed

#### Scenario: Analysis speed
- **WHEN** a 10-second loop at 44.1 kHz is analysed on the reference laptop
- **THEN** the result is available within 1 second
