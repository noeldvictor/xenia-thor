# Blue Dragon SoundBank Null Thunk

Date: 2026-05-18 10:05 EDT

## Question

Why did Blue Dragon crash at guest `0x826A23E8` after the `!necessity.ipk`
archive opened and read successfully?

## Evidence

Capture:

- `scratch/thor-debug/20260518-100510-logcat.txt`
- `scratch/thor-debug/20260518-100510-meta.txt`
- APK SHA-256:
  `248929CFD56A5FFD5C6ECE939E49B7D657D9E8FD6B91D63EA4C2C786D0FCD532`

The focused read trace showed:

```text
NtReadFile trace: path='\Device\Cdrom0\pack\!necessity.ipk'
request=3125248 position_before=0 bytes_read=3125248 status=00000000
```

The crash probe decoded the caller strings around `0x821904A0`:

```text
[Sound] 0x%.8X SoundBank Block_Read start [%s]..
Error... Sound::SOUNDBANK::Load hcFileMallocRead().
Error... Sound::SOUNDBANK::Load XACTCreateSoundBank().
[Sound] 0x%.8X SoundBank Block_Read finished [%s]..
```

The object passed through the null thunk started with `0x4B424453`, matching
`KBDS` / sound-bank data.

## Finding

The crash was not caused by the missing raw `D:\!necessity\font\...` paths.
The game had already opened and fully read `D:\pack\!necessity.ipk`.

The crash path was `Sound::SOUNDBANK::Load` after
`XACTCreateSoundBank()` failed. Android launches currently use `apu=nop`, but
`NopAudioSystem::CreateDriver` returned `X_STATUS_NOT_IMPLEMENTED`, causing the
guest audio/XACT initialization path to fail and leave the global at
`0x82785548` unset.

## Fix Tested

Added a real silent `NopAudioDriver` that accepts submitted frames, releases the
audio semaphore, and returns `X_STATUS_SUCCESS` from `NopAudioSystem`.

Validation capture:

- `scratch/thor-debug/20260518-100921-logcat.txt`
- `scratch/thor-debug/20260518-101159-logcat.txt`
- APK SHA-256:
  `D9D7A9155584D9D17F940CF04F20E836BC1DCB8D70FC8E8B3950822A66CC476E`

Result:

- `NopAudioSystem created silent audio driver 0`
- guest store watch hit:
  `826A2550@826A2598->82785548`
- no guest crash PC in the 10:09 and 10:11 captures
- Blue Dragon created the draw and sound threads

## Remaining Blocker

Blue Dragon is still not at title. The process remains alive, but the Thor is
spending CPU in slow ARM64 interpreter paths, especially:

- `82393310`
- `82393640`
- `823F2568`
- `822B54C0`

The next path is AArch64 mini-JIT coverage for those hot functions, not more
file/archive debugging.
