---
name: write-programming-tutorials
description: Write or revise hands-on programming tutorials in the style of the official Dart tutorials. Use when a learner asks for a walkthrough, lesson, numbered tutorial, or clearer instructions about where code goes and how to test it.
---

# Write hands-on programming tutorials

Use [Diátaxis: Tutorials](https://diataxis.fr/tutorials/) for lesson design and Dart's [Build your first app](https://dart.dev/learn/tutorial/first-app) and [Fetch data from the internet](https://dart.dev/learn/tutorial/fetch-data) for presentation. Borrow Dart's verb-led tasks, visible results, and comments that explain consequential lines, not Dart-specific markup.

## Keep the lesson learning-oriented

[Diátaxis](https://diataxis.fr/) distinguishes four reader needs: a **tutorial** guides a learner through a practical experience; a **how-to** helps an already-capable reader solve a task; **reference** gives facts to look up; **explanation** develops understanding. Keep one reliable path through the tutorial. Link to troubleshooting recipes, API reference, or deeper rationale rather than turning the lesson into all four at once.

Show the end result up front. Start with a small success, repeat the core action with one meaningful variation, and tell the learner what to notice at each checkpoint. Give enough local explanation to make the action intelligible without breaking the flow: Diátaxis's advice to minimize explanation is not a reason to omit the purpose of a step.

## Before writing

1. Read the repository's `AGENTS.md`, tutorial roadmap, previous lesson, and relevant sources and project files. Check which files and functions really exist, what builds them, and what is only planned. Don't claim a fixture or API exists because an older lesson mentions it.
2. Pick one concrete outcome and the smallest fixture that proves it. State the prerequisites, exact starting directory, shell/toolchain, and whether the learner is working in a scratch folder or integrating into the repo. Keep optional design extensions after the first working checkpoint.
3. Honor the repository's learning contract. In this engine, write Markdown under `tutorials/`, not project implementation. Default to C99 examples, label Windows-specific instructions, and keep actual verification builds outside the checkout. Preserve numbered filenames and update cross-links if you rename a lesson.

## Write each lesson as actions the learner can take

- **Goal and prerequisites:** Say what observable result the reader will get and link the exact prior lesson or setup step they need.
- **Tasks:** Use short, verb-led titles and numbered instructions when the order matters. For each snippet, tell the learner to **create** or **open** an exact path (relative to a named directory). Say whether to replace the entire file, add a declaration before `#endif`, insert a helper above `main`, or change a specific existing line. Put the path immediately before the code block in ordinary Markdown. Avoid bare instructions like “sketch the data structure” or “implement a parser.”
- **File roles and connections:** Explain why a header, source file, fixture, or generated file exists. Show at least one caller. For a generated artifact, show the command that produces it, where it lands, and who includes or loads it. If multiple files compile together, name them in the build command or say exactly which project consumes each file. A small file tree helps if paths cross directories.
- **Checkpoints:** Give a command run from a named directory, the expected output or exit status, and a common failure with a specific fix. After the happy path, ask the learner to change one thing and predict what happens. Make clear whether output was observed in verification or is only expected from the exercise.
- **Review and next lesson:** Summarize what the checkpoint proved and what it didn't. Separate scratch code from eventual integration and name the integration destination. Link the next lesson; add a short optional knowledge check only if it reinforces the concept.

## Explain decisions and comment the code

- **Explain why here and now.** Near each non-obvious step, state what problem it solves and what would go wrong without it. Tie the rationale to an observable checkpoint when possible. For example: “Build `memory` as a static library because it supplies functions, not an entry point. If you build it as an application, MSVC will look for `main`.” Distinguish a language requirement from a project decision or a simplification made for this fixture.
- **Comment the consequential lines.** In each substantial snippet, add terse, accurate comments where a learner might otherwise miss an invariant, ownership rule, ordering dependency, boundary check, or deliberate test value. Put the comment directly above the relevant line or block. For example, `/* Check the offset before subtracting so size_t cannot wrap. */` teaches more than `/* Check bounds. */`. Do not annotate every line or repeat the syntax. Keep comments valid for the snippet's language and compile mode.
- **Connect code and behavior.** After the snippet, point out what changed and what result proves it. To teach a design choice, prefer a safe variation with a predicted outcome over a long comparison of alternatives. Keep extended trade-offs in a short linked explanation or an optional section after the first passing checkpoint.

## Cite the sources the learner can verify

- Cite externally checkable claims about language rules, toolchain settings, APIs, ABI, and platform behavior close to the first relevant claim. Use descriptive Markdown links to the exact section of a primary source: a language standard or committee draft, official compiler or platform docs, or upstream library documentation. For example, link an assertion's control-flow behavior to the [GoogleTest assertions reference](https://google.github.io/googletest/reference/assertions.html), not a generic search result.
- For claims about this checkout, cite the relevant repository path or link to the actual declaration, project file, or fixture. Label proposed design choices as proposals; a source that documents the mechanism does not endorse the project's design. Attribute adapted snippets and check reuse terms when applicable.
- Verify that each cited page supports the specific claim and applies to the version or platform taught. Never invent a citation or claim you verified an inaccessible source. Add a short **Sources and further reading** section when it offers a useful route to deeper material; it does not replace links alongside key claims.

## Final pass

Read the lesson from a blank editor and fresh terminal: Can the learner name every file, put each snippet in its place, build and run it in order, and recognize success or failure? Resolve missing declarations, inputs, commands, and call sites, or label an incomplete snippet as an optional design sketch. If you verify runnable snippets, compile the text as written in a temporary directory and report what you actually ran. Do not fill in the project's implementation for the learner.

Before handing it off, check that prerequisites point backward in the roadmap, each non-obvious decision has a concise rationale, substantial snippets explain their critical lines, and key technical claims have verified citations. Remove any explanation that neither helps with the current action nor prepares the learner to interpret its result.
