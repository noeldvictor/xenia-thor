# Lost Odyssey Stall: Not Failing Game File-IO (iter 25)

## Result

The Lost Odyssey guest-spin stall worker is NOT blocked on a failing/never-
completing game file-IO op (unlike Banjo). The file-IO trace during the stall
shows normal `NtCreateFile` (guest 820008CC) and only ONE failing file query:

```
NtQueryFullAttributesFile status: path='ShaderDumpxe:\CompareBackEnds' status=C000000F missing=1
```

That path is a SHADER-DUMP diagnostic probe (`ShaderDump...`), not game content —
benign, present regardless of the stall. No failing game asset read appears.

So Lost Odyssey's never-completing worker (iter 24: XThread3F576CB0 stuck on
NtWaitForSingleObjectEx status=00000102) is most likely waiting on a **kernel
event/semaphore that is never signaled, or a worker-thread that never completes**,
NOT on file-IO. This separates it from Banjo's file-IO-error class.

## Packet

`scratch/thor-debug/game-pass-lo-disasm-20260529-233831` (complete run; an earlier
partial `...-233810` also exists). Lost Odyssey deterministic launch with
disassemble_functions + filter '827CACFC,822C5358', wait trace, file-IO trace.
Black (near_black=1, ~175 fps, vdswap=6279), no crash — same spin-stall state.

## Still open / next

The filtered PPC disassembly of 827CACFC (main spin) and 822C5358 (worker wait)
was not yet extracted (need the disasm emit tag; broaden grep next: try the bare
addresses '827CAC'/'822C53', 'disasm', 'PPC'). Goal: identify which guest object
(handle F8000240, guest_object 0014A018, type thread from iter 24) the worker
waits on and who is supposed to signal it — i.e. is the guest waiting on another
guest thread to finish (a thread-join that hangs) or on an event a kernel export
should set. That pinpoints the kernel-impl gap.

This is the live lead for the one true black title. Guest-execution/kernel lane,
not GPU, not file-IO. No code change (characterization).
