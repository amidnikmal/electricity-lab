# Learning Module

Date: 2026-06-10

Interactive lessons tied to the simulator: explanation -> generated task -> prediction -> attempt -> measurement through the engine -> explanation/critic. No motivational text, no "level/IQ" statements anywhere.

## Epistemic table

Only replicated results are used as mechanics. Strength grading: STRONG = decades / multiple replications; MODERATE = replicated with caveats; SINGLE-STUDY = one large recent study, marked as such.

| Claim | Strength | Source | How the app uses it |
| --- | --- | --- | --- |
| Testing effect / retrieval practice: recalling beats re-reading | STRONG | Roediger & Karpicke 2006 | Tasks demand an answer before any explanation is visible (attempt-first gating in code) |
| Distributed / spaced practice | STRONG | Cepeda et al. 2006; Dunlosky et al. 2013 | Export to Anki; spacing is scheduled by Anki's FSRS, not by mood and not by this app |
| Interleaving of problem types | MODERATE-STRONG | Rohrer & Taylor 2007; Dunlosky et al. 2013 | "Mixed practice" generates tasks with family-switch guarantee (no two same-family tasks in a row) |
| Worked examples / cognitive load | STRONG | Sweller (CLT) | Lesson arc shows one fully worked derivation before practice; tasks start at difficulty 1 |
| Self-explanation / elaborative interrogation | MODERATE | Chi et al. 1989; Dunlosky et al. 2013 | Solution text follows the derivation arc (situation -> action -> words -> symbols -> formula), never a bare formula |
| Generation effect: self-generated answers are retained better | STRONG | Slamecka & Graf 1978 | Predict-then-verify: a qualitative prediction is required *before* the numeric attempt is accepted (enforced in `LearningSession::canSubmitAttempt`) |
| Desirable difficulties | MODERATE-STRONG | Bjork | Difficulty levels; no hints before an attempt |
| Automation atrophy / automation bias | STRONG | Parasuraman & Riley 1997; FAA manual-flying requirements | Tool-free retrieval sessions: assistant and hints disabled by code, analogous to manual-flying practice |
| Cognitive offloading reduces retention | MODERATE | Risko & Gilbert 2016; Sparrow et al. 2011 (partially replicated) | The app never sends a task to the assistant without the user's recorded attempt |
| LLM access improves practice but harms unassisted performance unless tutor-scaffolded | SINGLE-STUDY (large field RCT, ~1000 students) | Bastani et al. 2024, "Generative AI Can Harm Learning" | Assistant is critic-not-solver; gating is code, not prompt; reliance metrics surfaced as facts |

Explicitly NOT used (not science or failed replication): IQ development / far-transfer brain training (n-back), learning styles, growth mindset as a strong effect (small/unstable in preregistered replications — may be mentioned as weak, never as foundation), neuromyths (10% brain, left/right brain). Futurology about "the role of humans post-AI" is prognosis, not science, and is not part of the content.

## AI-resilience safeguards (implemented as CODE, not prompt)

All six live in `src/learning/LearningSession.h`; a prompt can be argued around, these cannot:

1. **Attempt-first gating** — `canRevealSolution()` and the assistant gate require a recorded attempt; blocked tries are counted, not silently allowed.
2. **AI critic, not solver** — the only payload that can reach the LLM is `assistantContext()`: task statement + the user's prediction + the user's attempt + match/mismatch with the measurement. The solution text has no code path to the assistant. The system prompt (critic mode, one counter-question, never the final answer) is defense in depth only.
3. **Predict-then-verify** — `canSubmitAttempt()` is false until a qualitative prediction is recorded; generation by construction, delegation impossible.
4. **Tool-free checks** — `SessionMode::ToolFreeRetrieval` blocks the assistant unconditionally (manual-flying analog); these sessions are meant to be scheduled by Anki/FSRS spacing.
5. **Reliance metric** — `RelianceMetrics` counts help-before-attempt, blocked reveals, tool-free successes; shown in the panel as facts, no judgement, no motivational framing.
6. **Non-delegable manual core** — the derivation arc (situation -> action -> words -> symbols -> formula) is content the user walks through; the assistant can only ask where the user is stuck, because the arc text is never in its context.

## Task generation

`TaskGenerator` builds randomized circuits (Ohm, series, parallel, power, RC, RL; 3 difficulty levels). The ground truth is ALWAYS computed by the solver — DC MNA, or 1000 transient steps for RC/RL — never hard-coded (test recomputes every family independently). Consecutive tasks always switch family, so the next task is not a reverse-hint of the previous one.

## Anki / FSRS

Export via AnkiConnect (`POST localhost:8765`, action `addNotes`, model "Basic", tags `current-lab` + family). Scheduling is done by Anki's FSRS (default since Anki 23.10; difficulty / stability / retrievability model). The app supplies material and never computes intervals.

## Assistant portability

OpenAI-compatible `/v1/chat/completions` client (`src/assistant/LlmClient.*`) works with a local llama.cpp GGUF server (offline), a local vLLM box, or any compatible API (plain HTTP by design; use a local proxy for https). Honest limitation stated in the UI: a small local model is a weak tutor; quality scales with the model.
