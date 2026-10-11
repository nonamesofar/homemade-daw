---
name: "OPSX: Verify"
description: "Verify implementation matches change artifacts, then run an adversarial Opus code review with Sonnet fixes, before archiving"
allowed-tools: Bash(openspec:*), Bash(git diff:*), Bash(git status:*), Bash(git merge-base:*), Bash(git log:*)
category: "Workflow"
tags: ["workflow", "verify", "experimental"]
---

Verify that an implementation matches the change artifacts (specs, tasks, design).

**Store selection:** If the user names a store (a store is a standalone OpenSpec repo registered on this machine) or the work lives in one, run `openspec store list --json` to discover registered store ids, then pass `--store <id>` on the commands that read or write specs and changes (`new change`, `status`, `instructions`, `list`, `show`, `validate`, `archive`, `doctor`, `context`, `schemas`, `view`). Once selected, treat `--store <id>` as sticky for the rest of the workflow. Every unscoped example of those commands below is shorthand: before running it, append the flag. For example, run `openspec status --change "<name>" --json --store "<id>"`, not the unscoped form shown below. Other commands do not take the flag. Hints printed by commands already carry the flag; keep it on follow-ups. Without a store, commands act on the nearest local `openspec/` root.

**Project check:** These steps expect a project that already uses OpenSpec. Before the first step that writes anything (`new change`, `archive`, `sync specs`, or authoring an artifact file), confirm the project has a root: run `openspec list --json` (with `--store <id>` when a store is selected, since the store is then the root) and read `root`. A root object means the project is set up. `"root": null` means it is not - there is no `openspec/` directory here, and a write such as `openspec new change` would create one as a side effect. The command also exits non-zero, which is that answer rather than a broken CLI, so read the JSON instead of retrying or working around it.

One `"root": null` is not about setup: when a `status` error message starts with `Declared in` or `Invalid store declaration in` and names this project's `openspec/config.yaml` (or `config.yml`), the project does use OpenSpec through a store it declares, which this machine cannot resolve (the store is not registered, or the `store:` line is malformed). Do not treat it as uninitialized and skip the branches below: stop before writing and show the user that error's `message` and `fix`.

Otherwise, with no root, what happens next depends on how this workflow was reached:

- **Auto-selected**: you chose this workflow yourself, without the user naming OpenSpec, naming this skill, or running its slash command. Stop using OpenSpec and answer the request normally, as you would with no OpenSpec installed. Do not ask them to set anything up and do not mention OpenSpec setup.
- **Explicit OpenSpec request**: the user named OpenSpec, named this skill, or ran its slash command. Stop before writing and ask how to proceed: set this project up (`openspec init`), target a store they already have (`--store <id>`), or continue without OpenSpec for this request. Wait for their answer.

In both branches, never create the root as a side effect: do not run `openspec init` until the user asks for it, do not hand-create `openspec/` files, and do not let a command create it.

**Input**: Optionally specify a change name after `/opsx:verify` (e.g., `/opsx:verify add-auth`). Add `--review-only` to run the adversarial review without letting the coder change any code. If omitted, check if it can be inferred from conversation context. If vague or ambiguous you MUST prompt for available changes.

**Steps**

1. **Select the change**

   If a name is provided, use it. Otherwise:
   - Infer from conversation context if the user mentioned a change
   - Auto-select if only one active change exists
   - If ambiguous, run `openspec list --json` to get available changes and ask the user to select one

   When prompting, show all active changes returned by the list, including changes with `status: "no-tasks"`.
   Include the schema used for each change if available.
   Mark changes with incomplete tasks as "(In Progress)".

   Always announce: "Using change: <name>" and how to override (e.g., `/opsx:verify <other>`).

2. **Check status to understand the schema**
   ```bash
   openspec status --change "<name>" --json
   ```
   Parse the JSON to understand:
   - `schemaName`: The workflow being used (e.g., "spec-driven")
   - `planningHome`, `changeRoot`, `artifactPaths`, and `actionContext`: path and scope context
   - Which artifacts exist for this change

3. **Get planning context and load artifacts**

   ```bash
   openspec instructions apply --change "<name>" --json
   ```

   This returns the change directory, `contextFiles` (artifact ID -> array of concrete file paths), `taskTrackingConfigured`, and top-level `tasks` and `progress` aggregated from every concrete file matched by the schema's `apply.tracks` configuration that could be read. Read all available artifacts from `contextFiles`.

   Treat apply `state` and `instruction` as context, not a verification verdict. Do not implement tasks or archive the change during verification. The only code changes this workflow may make are the review fixes in step 8, made by the coder subagent.

4. **Initialize verification report structure**

   Create a report structure with three dimensions:
   - **Completeness**: Track tasks and spec coverage
   - **Correctness**: Track requirement implementation and scenario coverage
   - **Coherence**: Track design adherence and pattern consistency

   Each dimension can have CRITICAL, WARNING, or SUGGESTION issues.

   Verification is advisory. Respect intentional omissions such as `skip_specs: true`, optional design documents, and schemas without task tracking. Do not require or invent optional or intentionally omitted artifacts to obtain a clean report. `Not verified` describes a limit of this report, not a new archive prerequisite. Archive retains its own checks and user-confirmation behavior.
   Mark checks the schema does not define, or artifacts the status reports as intentionally skipped, as **Not applicable**. The correctness checks of a change whose readable delta specs contain REMOVED or RENAMED requirements but no ADDED or MODIFIED requirements are also **Not applicable** (see step 6). Exclude them from skipped-check counts and the archive-readiness assessment. Reserve **Not verified** for applicable checks whose evidence is missing or unusable.

   If only task evidence is available for applicable checks, verify task completion only and mark the remaining applicable checks, including **Code Pattern Consistency**, as not verified with the reason "Only task evidence available".

   If artifacts cannot be read or contain no usable requirements, scenarios, or design decisions, mark the affected checks as not verified with the specific reason. Continue checks supported by the remaining evidence, but a partially checked input set is not a fully verified check. Missing requirements affect Spec Coverage and Requirement Implementation Mapping; missing scenarios affect Scenario Coverage; missing design decisions affect Design Adherence.

5. **Verify Completeness**

   **Task Completion**:
   - If `taskTrackingConfigured` is false, report **Task Completion** as not applicable. Do not treat empty `tasks` as missing evidence.
   - Otherwise, use the top-level `tasks` and `progress` fields. They already aggregate every readable concrete file matched by `apply.tracks`, regardless of the tracked artifact's ID; do not infer tracking from a `contextFiles` key.
   - If `unavailableTrackingFiles` is nonempty, mark **Task Completion** as not verified and include every unavailable path and reason. Continue using any readable task evidence, but do not infer completion from the partial `tasks` and `progress` fields.
   - If `taskTrackingConfigured` is true and `tasks` is empty, mark **Task Completion** as not verified and record the reason from apply `state` and `instruction`. Nonzero totals alone do not establish evaluable task descriptions.
   - Report complete vs total tasks from `progress`.
   - If `progress.remaining` is greater than 0:
     - Add CRITICAL issue for each listed incomplete task. If the remaining count exceeds the listed incomplete tasks, also report the incomplete checkboxes without descriptions and recommend adding descriptions and completing them. Do not infer completion from the listed tasks alone.
     - Recommendation: "Complete task: <description>" or "Mark as done if already implemented"

   **Spec Coverage**:
   - If status marks the spec artifact skipped by `skip_specs: true`, or the schema defines no spec artifact (no artifact whose `artifactPaths.<id>.outputPath` is under `specs/`), report the spec-dependent checks as not applicable.
   - Otherwise, `contextFiles` is keyed by artifact id, and artifact ids come from the active schema, so do not assume an id such as `specs`. The spec artifacts are those whose `artifactPaths.<id>.outputPath` is under `specs/`; read their files from `contextFiles.<id>`. If those spec files are absent or empty, mark **Spec Coverage**, **Requirement Implementation Mapping**, and **Scenario Coverage** as not verified; do not treat any of them as clean.
   - If delta specs exist in those spec files:
     - Extract all requirements (marked with "### Requirement:", or listed as `FROM:`/`TO:` pairs under `## RENAMED Requirements`) and note the delta section each one sits under: `## ADDED`, `## MODIFIED`, `## REMOVED`, or `## RENAMED Requirements`. The section decides what the check looks for.
     - For each ADDED or MODIFIED requirement (for MODIFIED, check the text in the delta, not the old wording):
       - Search codebase for keywords related to the requirement
       - Assess if implementation likely exists
     - If ADDED or MODIFIED requirements appear unimplemented:
       - Add CRITICAL issue: "Requirement not found: <requirement name>"
       - Recommendation: "Implement requirement X: <description>"
     - For each REMOVED requirement, the change asks for the behavior to be gone, so invert the check:
       - Search codebase for the removed behavior. Matches in `openspec/` artifacts or docs, or in code that serves only the Migration note or an ADDED requirement, are not evidence by themselves. Report any code path that still delivers the removed behavior, including one shared with an ADDED requirement.
       - Finding no implementation is the expected result. Never report a REMOVED requirement as "Requirement not found" or recommend implementing it.
       - If the behavior is still present:
         - Add CRITICAL issue: "Removed requirement still implemented: <requirement name>"
         - Recommendation: "Remove the remaining implementation at <file>:<lines>, following the requirement's Migration note if it has one"
     - For each RENAMED entry (`FROM:`/`TO:`), the name changes but the behavior stays, so check the TO requirement for that unchanged behavior:
       - Do not report the FROM name as missing, and do not require code symbols, identifiers, or file names to be renamed.
       - If the TO name also appears under MODIFIED, its behavior is checked there against the MODIFIED text; skip it here.
       - Otherwise, read the baseline requirement in the main spec at `<planningHome.root>/openspec/specs/<capability-path>/spec.md`, using the same capability path as the delta spec: the requirement under the FROM name, or under the TO name only when the FROM name is absent because the main spec is already synced. Its body and scenarios are the evidence for the behavior the TO requirement keeps.
       - Search codebase for that behavior and assess if it is still implemented.
       - If it appears unimplemented:
         - Add CRITICAL issue: "Renamed requirement not found: <TO name>"
         - Recommendation: "Restore the behavior of <TO name> (renamed from <FROM name>); a rename must not change behavior"
       - If the baseline requirement cannot be found or read, mark **Spec Coverage** as not verified for that entry with the reason. Never count an unchecked rename as passing.

6. **Verify Correctness**

   If the delta specs are readable and contain at least one REMOVED or RENAMED requirement but no ADDED or MODIFIED requirements (the change only removes or renames requirements), report **Requirement Implementation Mapping** and **Scenario Coverage** as **Not applicable**. The REMOVED and RENAMED checks under Spec Coverage are the evidence for such a change (each RENAMED entry is checked there against its baseline behavior), so do not mark these two checks as not verified. A delta spec with no parseable requirements at all is unusable evidence, not a removal-only change: mark these checks as not verified.

   **Requirement Implementation Mapping**:
   - For each ADDED or MODIFIED requirement from delta specs (REMOVED entries, and RENAMED entries without a MODIFIED block, were settled under Spec Coverage):
     - Search codebase for implementation evidence
     - If found, note file paths and line ranges
     - Assess if implementation matches requirement intent
     - If divergence detected:
       - Add WARNING: "Implementation may diverge from spec: <details>"
       - Recommendation: "Review <file>:<lines> against requirement X"

   **Scenario Coverage**:
   - For each scenario under an ADDED or MODIFIED requirement in delta specs (marked with "#### Scenario:"):
     - Check if conditions are handled in code
     - Check if tests exist covering the scenario
     - If scenario appears uncovered:
       - Add WARNING: "Scenario not covered: <scenario name>"
       - Recommendation: "Add test or implementation for scenario: <description>"
   - Skip scenarios under a REMOVED requirement; that behavior is meant to be gone.

7. **Verify Coherence**

   **Design Adherence**:
   - If the schema defines no design artifact (no artifact with id `design`, and none whose `artifactPaths.<id>.outputPath` is or ends in `design.md`), report **Design Adherence** as not applicable.
   - If the design artifact's `contextFiles.<id>` file exists:
     - Extract key decisions (look for sections like "Decision:", "Approach:", "Architecture:")
     - Verify implementation follows those decisions
     - If contradiction detected:
       - Add WARNING: "Design decision not followed: <decision>"
       - Recommendation: "Update implementation or revise design.md to match reality"
   - Otherwise, if the design artifact's `contextFiles.<id>` file is absent or empty: mark **Design Adherence** as not verified. With other supporting artifacts, **Code Pattern Consistency** still runs; the task-only case remains limited to task completion.

   **Code Pattern Consistency**:
   - If implementation changes cannot be identified, mark **Code Pattern Consistency** as not verified and explain the missing evidence.
   - Otherwise, review new code for consistency with project patterns
   - Check file naming, directory structure, coding style
   - If significant deviations found:
     - Add SUGGESTION: "Code pattern deviation: <details>"
     - Recommendation: "Consider following project pattern: <example>"

8. **Adversarial review and fix (Opus reviewer, Sonnet coder)**

   Steps 5 to 7 check the code against the artifacts. This step attacks the code itself. It runs after step 7, so the reviewer can be given the findings so far, and it is the only part of this workflow allowed to change code. Skip it, and mark it **Not verified** with the reason, if no implementation files can be identified. If the user passed `--review-only`, run 8a to 8c but skip 8d to 8f: every finding is reported as "Not fixed (review-only)".

   **8a. Define the scope.** List the files this change touched: `git diff --name-only $(git merge-base HEAD main)` plus `git status --porcelain` for uncommitted work, narrowed to the implementation, test and build files that belong to the change (use the task list and the files found in steps 6 and 7). Exclude `external/`, generated files and binary fixtures. If the scope is large (more than about 25 files), split it by area and launch one reviewer per area in parallel.

   **8b. Launch the adversarial reviewer.** Use the Agent tool with `model: "opus"` and `subagent_type: "general-purpose"` (or "Explore" if you want it strictly read-only). The reviewer must not edit anything. Its prompt must be self-contained and include:
   - the change name, the paths of the artifacts (proposal, specs, design, tasks) and the in-scope file list
   - the findings from steps 5 to 7, so it does not repeat them and can challenge ones that look wrong
   - the project rules it must check against, taken from `AGENTS.md` (threading and ownership rules, real-time rules, time model, no clip overlaps, "audio files are never modified", dependency direction between `src/` modules, no platform `#ifdef` outside `src/platform/`)
   - an adversarial brief: assume the code is wrong and try to prove it. Hunt for correctness bugs, unhandled edge cases (empty, huge, corrupt or concurrent input), error paths that leak or crash, threading and lifetime problems, spec or design violations, data-loss risks, security issues, and tests that pass without testing anything. Do not report style nits or speculative refactors.
   - an output format: a numbered list where each finding has `id`, `severity` (CRITICAL / WARNING / SUGGESTION), `file:line`, what is wrong, a concrete failure scenario (input or state, then wrong result), and a suggested fix. End with a "Concerns" list for risks that are not concrete bugs.

   **8c. Triage.** Read the reviewer's list yourself and check each CRITICAL and WARNING against the code. Drop false positives and say why in the report. Sort what remains:
   - **Fixable**: a bug or rule violation with an unambiguous fix inside the change's scope.
   - **Needs a decision**: the fix would change a spec, a standing decision in `AGENTS.md`, the design, or the scope, or there are several reasonable fixes. Do not send these to the coder. Report them under open concerns.
   - **Out of scope**: an unfinished task or missing requirement from step 5 or 6 (that is `/opsx:apply` work, not a fix), or a problem outside the change's files. Report these, do not fix them.

   **8d. Launch the coder.** Skip if nothing is Fixable. Use the Agent tool with `model: "sonnet"`, once for all Fixable findings (several coders only when their files do not overlap). The prompt must include the Fixable findings verbatim, the in-scope files and the rules from `AGENTS.md`, and these constraints:
   - make the smallest change that fixes each finding, and match the surrounding code's style
   - add or adjust a test for every bug fixed, where the project's testing rules allow it
   - do not edit the OpenSpec artifacts, `ROADMAP.md` or `AGENTS.md`, do not tick tasks, and do not commit
   - build and run the affected tests (for example `cmake --build --preset windows-msvc-debug`, then `ctest --preset windows-msvc-debug`) and report the real output
   - report, per finding id: Fixed / Not fixed, the files changed, and the reason if not fixed

   **8e. Verify the fixes yourself.** Do not trust the coder's report. Read `git diff` for the files it changed, confirm that each "Fixed" finding is actually addressed, and run the build and tests again if the coder did not show passing output. A finding is only "Fixed" if the diff addresses it and the build and tests pass. If a fix broke the build or tests, mark that finding "Not fixed", note it in the report, and send it back to the coder once. A fix that is still broken after that is a CRITICAL issue.

   **8f. Re-review.** If any fixes were made, launch the Opus reviewer once more (same brief) on the fix diff only, to catch bugs the fixes introduced. Send new Fixable findings to the coder one last time. Stop after two fix rounds in total. Whatever is still open after that goes into the report as an open concern.

   After fixes, re-check anything in steps 5 to 7 that the fixes could have changed (for example a requirement or scenario that was flagged) and update the scorecard.

9. **Generate Verification Report**

   **Summary Scorecard**:
   ```markdown
   ## Verification Report: <change-name>

   ### Summary
   | Dimension    | Status           |
   |--------------|------------------|
   | Completeness | X/Y tasks, N reqs|
   | Correctness  | M/N reqs covered |
   | Coherence    | Followed/Issues  |
   ```

   In each Status cell, report the results of checks that ran and `Not verified (<reason>)` for every skipped check. If all checks in a dimension were skipped, start the cell with `Not verified`. Never score a skipped check as passing. Treat every not verified or partially verified check as skipped in the final assessment. Count only ADDED and MODIFIED requirements in N, and report REMOVED and RENAMED requirements separately (for example, "1 removal confirmed, 1 rename verified"). For a change that only removes or renames requirements, the Correctness cell reads `Not applicable (no ADDED or MODIFIED requirements)`.

   **Issues by Priority**:

   1. **CRITICAL** (Must fix before archive):
      - Incomplete tasks
      - Missing requirement implementations
      - Removed requirements still implemented
      - Renamed requirements whose behavior is no longer implemented
      - Each with specific, actionable recommendation

   2. **WARNING** (Should fix):
      - Spec/design divergences
      - Missing scenario coverage
      - Each with specific recommendation

   3. **SUGGESTION** (Nice to fix):
      - Pattern inconsistencies
      - Minor improvements
      - Each with specific recommendation

   **Final Assessment**:
   - Open CRITICAL findings from the adversarial review, and fixes that failed the build or tests, count as CRITICAL issues. Open WARNINGs count as warnings. Add one line: "Adversarial review: F fixed, N not fixed, D dismissed; C concerns remain."
   - If CRITICAL issues: "X critical issue(s) found. Fix before archiving." If any check was skipped, also name every skipped check and its reason.
   - If no CRITICAL issues, one or more warnings, and no checks were skipped: "No critical issues. Y warning(s) to consider. Ready for archive (with noted improvements)."
   - If only suggestions and no checks were skipped: "No critical issues or warnings. Z suggestion(s) to consider. Ready for archive (with noted improvements)."
   - If no issues and no checks were skipped: "All checks passed. Ready for archive."
   - If any check was skipped and there are no CRITICAL issues: do not claim readiness. Say "No critical issues found in the checks that ran. <check(s)> not verified: <reason>." Include the warning count when nonzero.
   - Include the suggestion count when nonzero in every final assessment.

The report ends with the Adversarial Review section described under "Output Format".

**Verification Heuristics**

- **Completeness**: Focus on objective checklist items (checkboxes, requirements list)
- **Correctness**: Use keyword search, file path analysis, reasonable inference - don't require perfect certainty
- **Coherence**: Look for glaring inconsistencies, don't nitpick style
- **False Positives**: When uncertain, prefer SUGGESTION over WARNING, WARNING over CRITICAL
- **Actionability**: Every issue must have a specific recommendation with file/line references where applicable

**Output Format**

Use clear markdown with:
- Table for summary scorecard
- Grouped lists for issues (CRITICAL/WARNING/SUGGESTION)
- Code references in format: `file.ts:123`
- Specific, actionable recommendations
- No vague suggestions like "consider reviewing"

   **Adversarial Review section** (always after the scorecard and issue lists; if step 8 was skipped, say so and give the reason):

   ```markdown
   ### Adversarial Review (Opus reviewer, Sonnet coder)
   Scope: <N files>. Rounds: <1 or 2>. Mode: <full | review-only>.

   | # | Severity | Location | Finding | Status | Notes |
   |---|----------|----------|---------|--------|-------|
   | 1 | CRITICAL | file.cpp:123 | <what the reviewer found> | Fixed | <what changed, test added> |
   | 2 | WARNING  | file.cpp:88  | <...> | Not fixed | <why: needs decision / out of scope / fix failed> |
   | 3 | WARNING  | file.cpp:40  | <...> | Dismissed | <why it is a false positive> |

   **Fixed:** <count>. **Not fixed:** <count>. **Dismissed:** <count>.

   **Remaining concerns:**
   - <risks the reviewer raised that are not concrete bugs, fixes that need a user decision, and anything still open after two rounds>

   **Code changed by the fixes:** <list of files>. Nothing is committed; review `git diff` before committing.
   ```

   Status is one of Fixed, Not fixed, or Dismissed. Never list a finding as Fixed unless step 8e confirmed it.
