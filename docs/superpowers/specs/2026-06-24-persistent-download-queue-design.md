# Persistent Download Queue and Process Shutdown Design

## Goal

Closing the application must stop all owned background work, including child processes spawned by downloads. Download tasks that the user did not explicitly delete must survive application restart and be available for manual resume.

## Scope

- Use a Windows Job Object for processes launched through `ProcessRunner`, so closing/canceling the owning run kills the process tree.
- Persist download queue state to `stuff/download_queue.json`.
- Restore all non-deleted queue entries on startup, including completed, failed, canceled, queued, and tasks that were running when the app closed.
- Convert running tasks to a canceled/stopped state during shutdown before persistence. Restored tasks must not auto-start; the user resumes them with the existing retry/resume action.
- Delete removes the task from persisted queue state. For canceled/failed tasks, existing partial-file cleanup remains tied to explicit delete/clear actions.

## Architecture

`ProcessRunner` owns a Job Object per process run. The child process starts suspended, is assigned to a kill-on-close job, then resumes. Cancellation and timeout close through the same process handle path, and the job handle guarantees child process cleanup.

`DownloadQueueStore` is a small core component responsible only for JSON serialization/deserialization of `DownloadTaskSnapshot` values. `DownloadQueue` remains responsible for runtime scheduling and state transitions. `Application` loads persisted snapshots after backend initialization and saves snapshots on queue changes and shutdown.

## Data model

`stuff/download_queue.json` stores a schema version and an array of tasks. Each task includes request fields, title, thumbnail path, user-visible state, progress fields, error text, and known output files. Running states are stored as `Canceled` with status `Остановлено` during shutdown.

## Error handling

Malformed or unreadable queue files are ignored safely: the app starts with an empty queue and logs the load failure. Save writes through a temporary file and then replaces the target. Individual invalid task entries are skipped rather than failing the entire load.

## Testing

- Unit test that queue snapshots round-trip through `DownloadQueueStore`.
- Unit test that active tasks are exported for persistence as stopped/canceled and restored without auto-starting.
- Unit test that delete removes the task from the persisted set.
- Process test that starts a parent command spawning a child process, cancels the run, and verifies the child process is no longer alive.

