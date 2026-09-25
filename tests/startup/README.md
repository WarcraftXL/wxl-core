# Extension startup completion regression

Compile StartupCompletionTests.cpp with an x86 MSVC developer shell and the repository src include directory:

```bat
cl /std:c++20 /EHsc /MT /W4 /I src tests/startup/StartupCompletionTests.cpp /Fe:startup_completion_test.exe
startup_completion_test.exe
```

Keep assertions enabled (do not define NDEBUG). This exercises the production completion gate: incomplete startup times out, a concurrent waiter remains blocked until completion, completion-before-wait is retained, repeated completion is harmless, and multiple waiters wake. The core worker calls WaitForLoadCompletion before Normal-phase registration/EnableAll; EngineInitDetour signals only after LoadAll and its EnableAll return. The wait runs outside DllMain and releases its mutex while waiting. A timeout logs an error and does not activate partial chains.

Live gate: cold-start at least twice with an extension that registers a Normal-phase hook. Inspect `wxl-core.log` to confirm extension loading finishes before `wxl-core ready`, with no rejected or partially enabled hook chain. Repeat without any extension installed to confirm the empty-folder path also completes.
