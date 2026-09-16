# AkidaTag model transfer over BLE: protocol specification

Scope: the BLE model-update transfer between BrainChip Connect and AkidaTag firmware.

This is the authoritative wire contract. The firmware in
`src/core/interface/ble_services/file_transfer.c` implements it and
`src/utils/send_model_via_ble.py` speaks it from a host; if either disagrees with
this page, this page is the bug report.

This document is the contract between the two repositories. It is written so the
phone side can be built from this page alone, without reading the firmware. Where
it says MUST the other side depends on it.

---

## 1. What this replaces, and why

Today the firmware collects a model into a single 102,236-byte SRAM buffer, flushes
that buffer to the AKD1500's SPI flash when it fills, and answers with a bare byte
`0xCC` that carries no position. The phone mirrors the same 102,236 constant as
`BUFFER_SIZE` in `bleManager.ts` and waits up to 10 s at each window.

Three things are wrong with it.

1. **The buffer is 102 kB.** It is the largest single allocation in the firmware, and
   it exists only because the transfer holds a whole window before writing any of it.
2. **Nothing carries a position.** The device never says how far it has got, so a
   desynchronised transfer is written to the wrong flash addresses and is only caught,
   if at all, by the CRC at the very end.
3. **The constant is mirrored in two repositories.** Change one and the other breaks
   silently.

The replacement is the shape used by Nordic's Secure DFU (4,096-byte objects), by
MCUmgr (an explicit offset in every request) and by the BrainChip Connect app's own
firmware path a few hundred lines away in the same file: **receive in pieces, hold a
bounded amount, write it to flash as it arrives, acknowledge with the position
committed, and check the whole file before trusting it.**

There is one path. The old shape is removed, not kept alongside. There is no version
negotiation and no fallback.

### Not in scope

- Resuming an interrupted transfer. A position is carried in every message because it
  is the right shape and because it makes desynchronisation detectable, but neither
  side offers to continue a transfer that stopped. An interrupted transfer starts again
  from zero.
- The model metadata record's own layout (`model_meta_t`, `model_data_meta_t`), and
  the metadata characteristics that fill it. Unchanged.
- The integrity check over the whole file, including its non-standard CRC32
  convention. Unchanged, and specified exactly in §7 so the app does not have to
  rediscover it.
- Firmware update over BLE. Untouched.

---

## 2. The shape in one picture

```
host                                                  device
 |                                                      |
 |-- aa05 app index, aa06 CRC, aa08..aa12, aa0e ------->|   session metadata
 |-- aa03  START(type=INFO, total_length) ------------->|
 |<------------------- aa02  OK(position=0) ------------|
 |                                                      |
 |-- aa01  (offset=0,    240 bytes) ------------------->|   staged
 |-- aa01  (offset=240,  240 bytes) ------------------->|   staged
 |   ...                                                |
 |-- aa01  (offset=2160, 140 bytes) ------------------->|   block complete
 |                                                      |   erase, program, verify
 |<------------------- aa02  DONE(position=2300) -------|
 |                                                      |
 |-- aa06 data CRC ------------------------------------>|
 |-- aa03  START(type=DATA, total_length) ------------->|
 |<------------------- aa02  OK(position=0) ------------|
 |                                                      |
 |-- aa01 x 18 (offsets 0 .. 3840) -------------------->|   one 4,096-byte block
 |                                                      |   erase sector, program
 |                                                      |   pages, read back, verify
 |<------------------- aa02  OK(position=4096) ---------|
 |   ... 24 more blocks ...                             |
 |<------------------- aa02  DONE(position=102236) -----|   whole file verified,
 |                                                      |   record stored
 |                                                      |   program the AKD1500,
 |                                                      |   run a test inference
 |<------------------- aa02  READY(position=102236) ----|   the model is live
```

The device holds at most one block. The host holds nothing it cannot resend, because
it never resends: it stops at each block boundary and waits.

---

## 3. GATT service

Service UUID `f000aa00-0451-4000-b000-000000000000`, unchanged.

All characteristic UUIDs are `f000aaXX-0451-4000-b000-000000000000`.

| UUID   | Name          | Properties                  | Payload                        | Status |
|--------|---------------|-----------------------------|--------------------------------|--------|
| `aa01` | Data          | Write, Write Without Resp.  | `u32 offset` + 1..N bytes      | **changed** |
| `aa02` | Status        | Notify (+ CCCD)             | 14 bytes, §6                   | **changed** |
| `aa03` | Control       | Write                       | `u8 opcode` + operands, §5     | **new** (UUID was allocated, never implemented) |
| `aa04` | File size     | —                           | —                              | **removed**, folded into `aa03 START` |
| `aa05` | App index     | Write                       | `u8`                           | unchanged |
| `aa06` | CRC32         | Write                       | `u32` LE                       | unchanged |
| `aa07` | Transfer type | —                           | —                              | **removed**, folded into `aa03 START` |
| `aa08` | Input shape   | Write                       | 1..3 x `u32` LE                | unchanged |
| `aa09` | Output shape  | Write                       | 1..3 x `u32` LE                | unchanged |
| `aa0a` | Flash address | Write                       | `u32` LE                       | unchanged |
| `aa0b` | Total length  | Write                       | `u32` LE                       | unchanged |
| `aa0c` | Is edge learned | Write                     | `u32` LE                       | unchanged |
| `aa0d` | Num edge classes | Write                    | `u32` LE                       | unchanged |
| `aa0e` | Model name    | Write                       | UTF-8, 1..63 bytes             | unchanged |
| `aa0f` | MFCC scalar   | Write                       | `u32` LE (IEEE-754 bits)       | unchanged |
| `aa10` | Silence class | Write                       | `u32` LE                       | unchanged |
| `aa11` | Unknown class | Write                       | `u32` LE                       | unchanged |
| `aa12` | Inference mode | Write                      | `u32` LE                       | unchanged |

All multi-byte fields everywhere in this protocol are **little-endian**.

Writes to `aa03`, `aa05`, `aa06` and `aa08`..`aa12` are Write Requests and are answered
at ATT level. Writes to `aa01` may be either Write Requests or Write Commands; see §5.2.

When `CONFIG_BT_LBS_SECURITY_ENABLED` is set the writable characteristics require an
encrypted link. That is the existing behaviour and is unchanged.

### 3.1 The status CCCD must be enabled first

The host MUST subscribe to `aa02` notifications before writing `aa03 START`. A START
that arrives with notifications disabled is refused with ATT error `0xFD`
(Client Characteristic Configuration Descriptor Improperly Configured), because a
transfer the device cannot report on is not worth beginning.

---

## 4. A session

A session is one INFO transfer followed by one DATA transfer, in that order, on one
connection.

**Step 1. Session metadata.** Before `START(INFO)` the host writes, in any order:

| Characteristic | Value |
|---|---|
| `aa05` | app index, `0` (the only slot today) |
| `aa0e` | model name, e.g. `/model_meta/kws` or `kws`; the device takes the text after the last `/` |
| `aa0b` | total length of info + data, in bytes, the value that goes into the stored header |
| `aa08` | input shape |
| `aa09` | output shape |
| `aa0a` | flash address the DATA half will be written to |
| `aa0c` | 1 for an edge-learning model, else 0 |
| `aa0d` | `(neurons_per_class << 16) | num_el_classes` |
| `aa0f` | MFCC normalisation scalar as IEEE-754 bits |
| `aa10` | silence class output index |
| `aa11` | unknown class output index |
| `aa12` | 0 = sync, 1 = async |
| `aa06` | `model_info_hdr_crc32`, computed as in §7.2 |

Every one of these except `aa06` is copied into the stored header, and the CRC in
`aa06` covers them, so they MUST all be written before `START(INFO)`. A value written
after START is used for the stored header but is not covered by the CRC the host
computed, and the transfer will be rejected with `ERR_INTEGRITY`.

**Step 2. INFO transfer.** `START(INFO, n)` where `n` is the size of the
`*_program_info.bin` file, then the bytes, then `DONE`. See §8.1 for what is stored.

**Step 3.** The host writes `aa06` again, now with the CRC32 of the whole
`*_program_data.bin` file (§7.1).

**Step 4. DATA transfer.** `START(DATA, n)` where `n` is the size of
`*_program_data.bin`, then the bytes in blocks, then `DONE`.

**Step 5. Installation.** After sending `DONE` for the DATA transfer, the device
programs the AKD1500 with the new model and runs a test inference, then sends one
further notification: `READY` if the model loaded and inferred, `ERR_PROGRAM` if it did
not. This takes a few seconds.

`DONE` and `READY` mean different things and the app must not conflate them. `DONE` is
"the file arrived and is safely stored"; `READY` is "the model is running on this
board". Reporting the first as though it were the second is the same lie as a refused
firmware update reporting success, which is a thing this project has already been
caught doing and has been told not to do. The app should drive its progress bar to 100%
on `DONE`, show *Installing*, and only claim the update succeeded on `READY`.

A host that disconnects after `DONE` does not stop the installation; it only misses the
result. The device completes the programming either way, and the same programming runs
at every boot from the record it just stored.

### 4.1 DATA requires INFO first

`START(DATA)` is refused with `ERR_STATE` unless, on this connection, a model name was
accepted on `aa0e` and a flash address was accepted on `aa0a`. The DATA transfer
deliberately carries no metadata of its own; it inherits the session's.

---

## 5. Messages the host sends

### 5.1 `aa03` Control

**START** — 6 bytes.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `0x01` |
| 1 | 1 | transfer type: `0x00` INFO, `0x01` DATA |
| 2 | 4 | `total_length`, bytes in this transfer |

Begins a transfer. Any transfer already in progress is discarded first, so a START is
always a clean beginning. The device answers with a status notification: `OK` with
`position = 0` if the transfer is armed, or an error code if it is not.

For DATA, START is where the device decides the transfer can succeed; it does not erase
anything. The first sector is erased when the first block is complete.

START is refused with `ERR_PARAM` if

- `total_length` is 0; or
- type is INFO and `total_length` > 2,300 (`MAX_MODEL_INFO_SIZE`); or
- type is DATA and the flash address from `aa0a` is not a multiple of 4,096; or
- type is DATA and `flash_address + total_length` exceeds 16,777,216; or
- the transfer type byte is neither 0 nor 1.

and with `ERR_STATE` if type is DATA and §4.1 is not satisfied.

A START whose payload is not exactly 6 bytes is refused at ATT level with `0x0D`
(Invalid Attribute Value Length).

**ABORT** — 1 byte, `0x02`.

Abandons the transfer in progress. The device discards its staging buffer and its
counters and answers `ABORTED` with `position = 0`. An ABORT with no transfer in
progress is not an error; the device answers `ABORTED` too. See §9.4 for what is left
on the device.

An unknown opcode is refused at ATT level with `0x13` (Value Not Allowed).

### 5.2 `aa01` Data

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `offset`, the absolute byte offset of this payload within the file |
| 4 | 1..N | payload |

`N` is bounded by the negotiated ATT MTU: the whole write is at most `ATT_MTU - 3`
bytes, of which 4 are the offset. At the 247-byte MTU a phone usually gets, that is
240 payload bytes per write.

**The host SHOULD use Write Without Response.** It is several times faster than a
Write Request per chunk, and it is safe here precisely because every write carries its
offset: a write that goes missing is caught by the next one, immediately, rather than
corrupting the flash silently. Write Requests are accepted and behave identically, for
a host whose stack paces better that way.

Two rules bind the host.

1. **`offset` MUST equal the position the device is expecting.** That is `0` for the
   first write of a transfer, and thereafter the previous write's `offset + payload
   length`. A mismatch ends the transfer with `ERR_OFFSET`.
2. **A write MUST NOT cross a block boundary.** The payload length MUST be at most
   `min(block_size - (offset mod block_size), total_length - offset)`. A write that
   crosses ends the transfer with `ERR_PARAM`.

Rule 2 is what keeps the position in the acknowledgement exact: every status the host
receives is a boundary the host also knows about, so `position` from the last status
is always the offset of the next write.

**`aa01` never returns an ATT error.** Every outcome, including a malformed write
shorter than 5 bytes and a write with no transfer in progress, is reported as a status
notification on `aa02`. A Write Request to `aa01` is always answered success at ATT
level. This keeps the two write modes indistinguishable.

---

## 6. The status notification, `aa02`

14 bytes, always.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `result`, §6.1 |
| 1 | 1 | transfer type this refers to: `0x00` INFO, `0x01` DATA |
| 2 | 4 | `block_size`, bytes |
| 6 | 4 | `position`, bytes |
| 10 | 4 | `total`, the `total_length` of the transfer in progress |

`block_size` is 4,096 today. **The host MUST read it from the status rather than
hard-coding it**, which is the whole point of putting it here: the 102,236 mirrored in
two repositories is the mistake this protocol exists to stop repeating. The value is
constant for the life of a transfer, so reading it from the START status is enough.

`position` is **the byte offset the device expects in the next write to `aa01`**.

- For `OK` and `DONE` the device has committed everything below `position` to its final
  destination and read it back to prove it. For those two codes, and only those two,
  "expects next" and "committed and verified" are the same number, because the device
  sends them only at block boundaries.
- For `ERR_OFFSET` it is the offset the device wanted, against the one it got.
- For the other errors it is where the device was when it gave up, which is
  diagnostic only.

`total` is an echo of `total_length` from START, so each notification is
self-describing and the app can render progress without holding state.

When no transfer is in progress, `transfer type` and `total` are whatever the last
transfer set them to and carry no meaning.

### 6.1 Result codes

| Code | Name | Meaning | What the host does |
|---|---|---|---|
| `0x00` | `OK` | START accepted, or a block is committed and verified. | Continue from `position`. |
| `0x01` | `DONE` | The whole file arrived, its integrity check passed, and the record is stored. `position == total`. | INFO: finished. DATA: wait for `READY`. |
| `0x02` | `ERR_OFFSET` | A write arrived at the wrong offset. | Transfer is dead. Start again from zero. |
| `0x03` | `ERR_INTEGRITY` | The whole-file check failed: CRC32 mismatch, or for DATA the readback from flash did not match. | Transfer is dead. Start again from zero. §9.1. |
| `0x04` | `ERR_FLASH` | A sector erase, a page program, or a block readback failed. | Transfer is dead. Start again from zero. §9.2. |
| `0x05` | `ERR_STATE` | A message arrived that makes no sense here: data with no transfer armed, `START(DATA)` before `START(INFO)`. | Fix the ordering. |
| `0x06` | `ERR_PARAM` | START parameters were rejected, or a data write crossed a block boundary or was malformed. | Fix the request. |
| `0x07` | `ABORTED` | ABORT accepted, or the device discarded the transfer itself. | Transfer is gone. |
| `0x08` | `READY` | DATA only. The stored model is programmed into the AKD1500 and a test inference passed. | The update succeeded. Say so now, not before. |
| `0x09` | `ERR_PROGRAM` | DATA only. The file is stored and verified, but the AKD1500 did not accept it or the test inference failed. | §9.6. |

`READY` and `ERR_PROGRAM` are terminal and follow the DATA `DONE` by a few seconds.
They are the only codes that arrive without the host having written anything, and they
are never sent for an INFO transfer, which installs nothing.

**Every code except `OK`, `DONE` and `READY` ends the transfer.** The device discards
its staging buffer and its counters, and the next thing it will accept is a START.
There is no partial state to pick up, by design.

### 6.2 How long the host waits

The device notifies once per block, so the host waits at exactly the points it knows
about.

| Wait | Recommended timeout | Why |
|---|---|---|
| Status after START | 2 s | Parameter validation and, for DATA, unlinking one small file. |
| Status after a block that is not the last | 5 s | Erase one sector (~20 ms typical), program eight to sixteen pages, read the sector back. The flash driver's own per-sector status poll gives up at 1,000 ms, so 5 s covers a slow part with margin. |
| Status after the last block | 30 s | The last block additionally carries the whole-file integrity check, which re-reads the entire file out of flash: ~200 ms for a 100 kB model, longer for a large one on a slow bus. |
| `READY` or `ERR_PROGRAM` after a DATA `DONE` | 60 s | Programming the AKD1500 and running one test inference. A few seconds in practice; the budget is loose because there is nothing to gain from a tight one. |

A timeout means the device is not going to answer. The host should ABORT, then either
retry the transfer from zero or disconnect.

The one exception is a timeout waiting for `READY`: the transfer itself is already
finished and the record is stored, so ABORT has nothing to abort. Treat it as §9.6 with
an unknown cause.

---

## 7. Integrity, unchanged

The CRC32 is the reflected IEEE 802.3 polynomial `0xEDB88320`, but applied with a
**non-standard convention that both sides already implement and that this change does
not touch**: the running state starts at `0xFFFFFFFF` rather than `0`, and the final
value is XORed with `0xFFFFFFFF`. Applied to the standard incremental primitives
(`zlib.crc32` in Python and JavaScript's equivalent, `crc32_ieee_update` in Zephyr),
that double inversion means the result is **not** the plain CRC32 of the bytes. It is
what the firmware stores and what the firmware compares against, so the app must
reproduce it exactly.

In Python, and this is the existing helper in `src/utils/send_model_via_ble.py`:

```python
crc = 0xFFFFFFFF
for chunk in chunks:
    crc = zlib.crc32(chunk, crc)
result = (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF
```

In TypeScript, using any incremental CRC32 with the same `(data, seed)` contract:

```ts
let crc = 0xFFFFFFFF;
for (const chunk of chunks) crc = crc32(chunk, crc);
const result = (crc ^ 0xFFFFFFFF) >>> 0;
```

### 7.1 The DATA CRC

CRC32, as above, over every byte of `*_program_data.bin`. Written to `aa06` before
`START(DATA)`.

### 7.2 The INFO CRC

CRC32, as above, over **124 bytes of packed header fields followed by every byte of
`*_program_info.bin`**. The header bytes are the `model_meta_t` struct from
`total_length` onwards, that is the whole struct except its own CRC field. All fields
are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `total_length` (info bytes + data bytes) |
| 4 | 12 | `input_shape[3]`, zero-padded |
| 16 | 12 | `output_shape[3]`, zero-padded |
| 28 | 4 | `flash_address` |
| 32 | 4 | `is_edge_learned` |
| 36 | 4 | `num_edge_classes`, `(neurons << 16) | classes` |
| 40 | 4 | `info_data_len`, the size of `*_program_info.bin` |
| 44 | 4 | `mfcc_fs_bits`, IEEE-754 bits of the scalar |
| 48 | 4 | `silence_class` |
| 52 | 4 | `unknown_class` |
| 56 | 4 | `inference_mode` |
| 60 | 64 | `model_name`, UTF-8, NUL-padded to 64 bytes |

`model_name` is the name alone, e.g. `kws`, not the `/model_meta/kws` form written to
`aa0e`. The device takes the text after the last `/` of whatever `aa0e` carried, so the
two agree.

The reference implementation is `compute_combined_crc32()` in
`src/utils/send_model_via_ble.py` and it is unchanged by this work.

---

## 8. What the device does with the bytes

### 8.1 INFO

`total_length` is at most 2,300, which is less than one block, so an INFO transfer is
always exactly one block and produces exactly two notifications: `OK` at START and
`DONE` at the end.

The device stages the bytes, computes the CRC of §7.2 over the header it assembled
from the session metadata plus the staged bytes, and compares it with `aa06`. On a
mismatch it answers `ERR_INTEGRITY` and writes nothing. On a match it writes two
LittleFS files and answers `DONE`:

- `/ext/<name>_model_hdr` — the 128-byte `model_meta_t`
- `/ext/<name>_model_info` — the raw `*_program_info.bin` bytes

`<name>` comes from `aa0e` and must be one the firmware knows; today the only one is
`kws`. An unknown name is rejected at `START(INFO)` with `ERR_PARAM`.

### 8.2 DATA

`total_length` is split into blocks of `block_size`, the last one short. A 102,236-byte
model is 24 full blocks plus 3,932 bytes, so 25 blocks and 25 status notifications
after START.

For each completed block the device, in this order:

1. computes a CRC over the staged bytes;
2. erases the one 4,096-byte sector at `flash_address + block_start`;
3. programs the staged bytes into that sector, in 256-byte pages;
4. reads the sector back and checks the CRC matches;
5. sends `OK` with `position = block_start + block_length`, or `ERR_FLASH` if step 2,
   3 or 4 failed.

Erasing a sector at a time rather than the whole region up front is deliberate. It
means the flash is never left erased-but-unwritten for longer than one block, and a
flash that stops answering is caught at the block where it happened rather than at the
end of a 100 kB transfer.

Because erase granularity is a whole sector, the tail of the last sector beyond
`total_length` is left at `0xFF`. That was true before this change too.

After the last block the device additionally:

6. compares the CRC32 of §7.1, accumulated over everything it received, with `aa06`;
7. re-reads the entire file out of flash and checks both its first four bytes and its
   CRC32 against what it just received;
8. writes `/ext/<name>_model_data_hdr`, the 12-byte `model_data_meta_t` holding the
   CRC, the first four bytes and the length;
9. sends `DONE`;
10. programs the AKD1500 and runs a test inference;
11. sends `READY`, or `ERR_PROGRAM` if step 10 failed.

Steps 6 and 7 are the "check the whole file before trusting it" half of the standard,
and step 8 comes after both of them: the record that says a model is present is only
written once the bytes have been proven to be in flash. A failure at 6 or 7 answers
`ERR_INTEGRITY` and the record is not written.

`DONE` is sent at step 9 rather than after step 11 so the app can show honest progress:
the transfer is genuinely complete there, and what follows is installation, which is a
different thing taking a different length of time.

---

## 9. Failure, in detail

The question each of these answers is: what is on the device afterwards, and what can
the user do next.

### 9.1 The integrity check fails

**INFO.** Nothing is written. The previously stored header and info files are still
there, intact, describing the model the board already had. The board keeps running
that model. The host may START again immediately.

**DATA.** The flash holds a mixture: the blocks that arrived, and whatever was in the
remaining sectors before. The data record `/ext/<name>_model_data_hdr` has already been
deleted (§9.5), so no record claims those bytes are a model. The board therefore has no
usable model until a DATA transfer completes: at the next boot the firmware finds the
header but no data record, logs that the model data file is not present, and does not
program the AKD1500. Keyword spotting does not run. The fix is another full transfer.

This is the same exposure the previous protocol had, for the same reason: a model being
replaced in place cannot also be kept.

### 9.2 The flash fails

`ERR_FLASH` means a sector erase, a page program or a block readback did not do what it
was asked. On AkidaTag the usual cause is the AKD1500 being unreachable, since the model
flash hangs off the AKD1500's SPI feedthrough and a sleeping chip feeds nothing through.
The device state afterwards is exactly §9.1 DATA: a partly written region and no data
record. The serial log carries the diagnosis; the app can only report the failure and
offer to try again.

### 9.3 The position goes wrong

`ERR_OFFSET` means the device received a write at an offset it was not expecting: a
write was lost, duplicated, or the host's own counter drifted. The device gives up
rather than guessing. Its `position` field tells the app what the device wanted, which
is worth logging because the gap is the evidence. Device state is §9.1 for whichever
transfer was running.

This code should never be seen. If it is, something is wrong with the host's chunking
or its link, and retrying blindly is likely to hit it again.

### 9.4 The transfer is abandoned part way

Three ways it can happen, with the same result.

- The host writes `ABORT` on `aa03`. The device answers `ABORTED`.
- The BLE link drops. The device abandons the transfer on disconnect; no notification
  is possible.
- The host simply stops writing and stays connected. The device waits indefinitely; it
  has no inactivity timer, because it is holding no resource anyone else wants. The
  next START clears it.

In all three the device discards the staging buffer and the counters, and the flash and
LittleFS are left exactly as the last completed block left them. For DATA that is §9.1:
a partly written flash region and no data record, so the board has no model until a
transfer completes. For INFO nothing has been written at all and the board is
untouched.

A transfer that is abandoned is not resumable. Starting again means `START` at zero and
sending every byte.

### 9.5 Why the data record is deleted at the start

`START(DATA)` deletes `/ext/<name>_model_data_hdr` before the first sector is erased.

The moment the first sector is erased, any existing record describing that flash region
is a lie: it names a length and a CRC for bytes that are no longer there. Leaving it in
place means an abandoned transfer boots into a CRC mismatch, which reads like a
corrupted board. Deleting it up front means an abandoned transfer boots into "there is
no model", which is the truth and is a state the firmware already handles cleanly.

The header and info files are **not** deleted at `START(INFO)`, because the INFO
transfer commits in one step at the end and so can leave the old ones intact until it
succeeds.

### 9.6 The model is stored but will not install

`ERR_PROGRAM` is the one failure where the transfer itself succeeded. The bytes are in
flash, they have been read back and verified twice, and the record is written. What
failed is the AKD1500 refusing the model, or the test inference after it.

The device state is **a complete, valid, stored model that this board cannot run.** The
record is deliberately left in place, for two reasons: a transient failure then fixes
itself at the next boot, because the boot path programs from exactly the same record;
and a permanent failure is no worse off, because the board has no working model either
way.

**There is no retry-the-programming command.** The protocol offers one recovery action
and it is a fresh transfer, which is the path the app already has. That is the simpler
answer for the app, and the honest one: the two realistic causes are a model the
firmware is not compatible with and an AKD1500 that is not responding, and neither is
fixed by asking the device to try the same bytes again.

So the app should report that the model was delivered but did not start, and offer the
two things that can actually help: restart the device, which retries the installation
from the stored record and is already available as `CMD_RESTART` over NUS; or transfer
a different model. It should not present a *retry* that silently re-sends the same
bytes and fails the same way.

---

## 10. Host pseudo-code

```ts
const START = 0x01, ABORT = 0x02;
const INFO  = 0x00, DATA  = 0x01;
const OK = 0x00, DONE = 0x01, READY = 0x08;

async function transfer(type: number, bytes: Uint8Array) {
  // aa02 must already be subscribed, and the session metadata already written.
  let status = await writeControlAndAwaitStatus(startFrame(type, bytes.length));
  if (status.result !== OK) throw new TransferError(status);

  const blockSize = status.blockSize;   // from the device, never hard-coded
  const maxPayload = mtu - 3 - 4;       // 4 bytes of offset per write

  let offset = 0;
  while (offset < bytes.length) {
    const blockEnd = Math.min(
      (Math.floor(offset / blockSize) + 1) * blockSize,
      bytes.length,
    );
    while (offset < blockEnd) {
      const n = Math.min(maxPayload, blockEnd - offset);
      await writeDataNoResponse(offset, bytes.subarray(offset, offset + n));
      offset += n;
    }
    status = await awaitStatus(offset === bytes.length ? 30_000 : 5_000);
    if (status.result !== OK && status.result !== DONE) throw new TransferError(status);
    if (status.position !== offset) throw new DesyncError(status.position, offset);
  }

  if (status.result !== DONE) throw new TransferError(status);

  // The file is stored and verified. It is NOT yet running on the board.
  if (type === DATA) {
    onTransferComplete();                       // progress 100%, "Installing"
    const installed = await awaitStatus(60_000);
    if (installed.result !== READY) throw new InstallError(installed);
    onUpdateSucceeded();                        // only now
  }
}
```

`status.position !== offset` should be impossible given rule 2 of §5.2, and asserting
it is cheap. It is the check that turns a protocol mistake on either side into a clear
error instead of a corrupted model.

---

## 11. Constants

| Name | Value | Where it comes from |
|---|---|---|
| `block_size` | 4,096 | one flash sector on the MT25QU128ABA. **Read it from the status notification.** |
| flash page | 256 | internal to the firmware; the host never sees it |
| `MAX_MODEL_INFO_SIZE` | 2,300 | largest INFO transfer |
| model name length | 63 bytes + NUL | `aa0e` and the stored header |
| flash size | 16,777,216 | bounds `flash_address + total_length` |
| flash address alignment | 4,096 | `START(DATA)` rejects anything else |
| ATT MTU | negotiated, 498 offered | payload per `aa01` write is `MTU - 7` |

---

## 12. Memory actually handed back

The 102,236-byte `sram_upload_buffer` is not the transfer's alone. It is shared, under a
Zephyr `k_event`, with the SPI camera, which captures a 128x128 RGB888 frame into it and
needs 49,152 bytes; and with the raw audio capture test, which is behind
`CONFIG_AUDIO_CAPTURE_TEST` and is off in every shipped build.

So shrinking the transfer to one sector does not hand back 98 kB, as the earlier plan
page said. It hands back the difference between the old shared buffer and the largest
claimant that remains:

| | Bytes |
|---|---|
| Today, one shared buffer | 102,236 |
| After: camera frame buffer | 49,152 |
| After: model transfer staging | 4,096 |
| **After, total** | **53,248** |
| **Handed back** | **48,988** |

48,988 bytes. The camera's 49,152 was always being paid for; it was just hidden
inside a buffer named after something else, so the headline "shrink 102,236 to
4,096" overstates the saving by roughly a factor of two.

Measured rather than argued: the application core's RAM report goes from **370,120
bytes (80.68% of 448 kB) to 321,096 bytes (69.99%)**, a saving of **49,024 bytes**. The
36 bytes beyond the buffer arithmetic are the `k_event` and the transfer counters that
went with it.

Two consequences beyond the arithmetic.

- **The `k_event` goes away.** It exists only to stop the camera and the model update
  writing into the same memory at the same time. The camera is on SPI3 and the AKD1500
  flash is on SPI4, so with separate allocations there is nothing left for it to guard,
  and a lock that guards nothing is worse than no lock. `BUF_EVENT_FREE`,
  `BUF_EVENT_BUSY`, `sram_buf_event` and `shared_buf_init()` are removed.
- **The audio capture test gets its own buffer**, still sized by
  `CONFIG_SRAM_BUFFER_SIZE` and still costing nothing when the option is off. It never
  took the event, so it was racing with both other users; now it cannot.

---

## 13. Migration checklist for BrainChip Connect

Everything below is in `bleManager.ts`.

- [ ] Delete the `BUFFER_SIZE = 102236` constant at line 1704 and every use of it. The
      block size comes from the device.
- [ ] Stop writing `aa04` (file size) and `aa07` (transfer type). Both characteristics
      are gone; write `aa03 START` instead, which carries both.
- [ ] Prefix every `aa01` write with the 4-byte little-endian absolute offset, and cap
      the payload at the block boundary.
- [ ] Switch `aa01` to Write Without Response.
- [ ] Parse `aa02` as the 14-byte record of §6 instead of a single byte. The codes
      `0xEE`, `0xCC` and `0xBB` no longer exist.
- [ ] Wait for a status at every block boundary, not every 102,236 bytes, and check
      `position` against the host's own offset.
- [ ] Replace the flat 10 s timeout with the four timeouts of §6.2.
- [ ] Send `ABORT` when the user cancels, instead of just dropping the link.
- [ ] **Wait for `READY` after the DATA `DONE` before telling the user the update
      succeeded.** `DONE` drives the progress bar to 100% and shows *Installing*;
      `READY` is the success. `ERR_PROGRAM` is a delivered-but-not-running model, §9.6,
      and the user is told that rather than "complete".
- [ ] Surface the result codes of §6.1; `ERR_INTEGRITY` and `ERR_FLASH` mean the board
      has no model until a transfer completes, and the user needs to be told that.
- [ ] Nothing about the CRC32 computation changes. Do not "fix" the double inversion.

The firmware lands first. Until the app catches up, model update from the app fails on a
board carrying the new firmware; firmware update is on a separate channel and is
unaffected, so a board can always be moved between the two.

---
