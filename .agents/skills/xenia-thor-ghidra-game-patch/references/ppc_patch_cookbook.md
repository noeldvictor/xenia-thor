# PowerPC / Xenon patch cookbook (for .patch.toml `be32` values)

Xbox 360 is 32-bit-fixed-width big-endian PowerPC (Xenon, based on PPC). Every
instruction is 4 bytes. A `[[patch.be32]]` with `value = <word>` overwrites the
instruction at `address` with that big-endian word (the patcher byte-swaps, so
you write the word as a normal integer — e.g. `0x60000000`).

## Most-used replacement words

| Intent                                   | Instruction      | `value`      |
|------------------------------------------|------------------|--------------|
| Do nothing (erase an instruction)        | `nop` (`ori 0,0,0`) | `0x60000000` |
| Return immediately                       | `blr`            | `0x4E800020` |
| Return 0 (often: `li r3,0` then `blr`)   | `li r3,0`        | `0x38600000` |
| Return 1 (`li r3,1`)                     | `li r3,1`        | `0x38600001` |
| Unconditional skip near (`b .+8`)        | `b $+8`          | `0x48000008` |
| Force a taken branch never taken         | replace `bc` with `nop` | `0x60000000` |

`li rD, imm` = `0x38000000 | (D<<21) | (imm & 0xFFFF)`.
`addi rD,rA,imm` = `0x38000000 | (D<<21) | (A<<16) | (imm&0xFFFF)`.
`ori rD,rA,imm` = `0x60000000 | (A<<21) | (D<<16) | (imm&0xFFFF)` (note A/S swap).

## Flipping a conditional branch (the dirty-disc / "skip the bad path" move)

Conditional branch `bc BO,BI,target` = opcode 16 (`0x40000000`).
- `beq` (branch if equal)  : BO=12, BI=2  → `0x41820000 | (disp&0xFFFC)`
- `bne` (branch if not eq) : BO=4,  BI=2  → `0x40820000 | (disp&0xFFFC)`
- `blt`: `0x41800000`  `bge`: `0x40800000`  `bgt`: `0x41810000`  `ble`: `0x40810000`

To **invert** a branch in place, flip the BO "branch-true/false" bit: change the
high halfword between `0x4182`↔`0x4082` (eq↔ne), `0x4180`↔`0x4080` (lt↔ge),
`0x4181`↔`0x4081` (gt↔le); keep the low 16 bits (the displacement) unchanged.

To **always skip** the error path instead, just `nop` the branch
(`value = 0x60000000`) if the fall-through is the good path, or replace it with
an unconditional `b` to the good target (opcode 18 = `0x48000000 | (disp&0x03FFFFFC)`,
disp is signed, relative to the branch address).

Read the function before flipping — make sure the "good" path doesn't skip setup
the title needs.

## Value-type cheatsheet (`[[patch.<type>]]`)

| type        | use                                   | example value            |
|-------------|---------------------------------------|--------------------------|
| `be8`       | 1 byte                                | `0x01`                   |
| `be16`      | 2-byte int (big-endian)               | `0x0001`                 |
| `be32`      | 4-byte int / PPC instruction word     | `0x60000000`             |
| `be64`      | 8-byte int                            | `0x0000000000000001`     |
| `f32`       | 32-bit float constant                 | `60.0`                   |
| `f64`       | 64-bit float constant                 | `60.0`                   |
| `string`    | ASCII bytes (no length swap)          | `"value"`                |
| `u16string` | UTF-16 bytes                          | `"value"`                |
| `array`     | raw hex byte sequence                 | `"60000000 60000000"`    |

Each `[[patch.<type>]]` table has `address` and `value`. Multiple writes (e.g. a
multi-instruction patch) are multiple tables of the same type at `address`,
`address+4`, …

## Tips

- Ghidra's listing address **is** the `address` (base `0x82000000`). No
  translation needed.
- After assembling a replacement in Ghidra (Patch Instruction), read the raw
  bytes from the Bytes view — that big-endian word is your `be32 value`.
- For a "freeze value" cheat where the game writes the resource each frame,
  prefer NOPing the **decrement/store** over patching the value in memory (the
  game would just overwrite a memory poke).
