---
name: write-programming-tutorials
description: Write or revise hands-on programming tutorials in the style of the official Dart tutorials. Use when a learner asks for a walkthrough, lesson, numbered tutorial, or clearer instructions about where code goes and how to test it.
---

# Write hands-on programming tutorials

Use Dart's [Build your first app](https://dart.dev/learn/tutorial/first-app) and [Fetch data from the internet](https://dart.dev/learn/tutorial/fetch-data) as style references. Borrow the teaching sequence, not Dart-specific markup: the lessons state what you'll accomplish, list prerequisites, walk through named tasks in order, show the results, and review what you learned before the next lesson.

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

## Final pass

Read the lesson from a blank editor and fresh terminal: Can the learner name every file, put each snippet in its place, build and run it in order, and recognize success or failure? Resolve missing declarations, inputs, commands, and call sites, or label an incomplete snippet as an optional design sketch. If you verify runnable snippets, compile the text as written in a temporary directory and report what you actually ran. Do not fill in the project's implementation for the learner.
