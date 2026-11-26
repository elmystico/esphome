# I2C vote bus (`i2c_vote`)

Draft documentation. For an upstream pull request this text moves to the esphome.io repository.

## Why

I2C has no checksum and no parity bit. The only check is the acknowledge bit after each byte, and during a
read it is the controller that sends it. When a glitch on the clock line (a poor connector, ringing, noise)
makes a device lose bit sync, the controller reads wrong bytes and still acknowledges them. The driver gets
a wrong value with no error.

Typical patterns seen on an ADS1015/ADS1115 behind a poor connector:

- trailing ones, such as `0x1FFF` or `0x7FFF`: the device stopped driving the data line part way through a
  byte, so the pull-up resistor was read as ones;
- a repeated byte, such as `0xE3E3` or `0x4A4A`: the device sent the same byte twice.

## How it works

`i2c_vote` is a virtual bus, like a `tca9548a` channel. Devices that are moved onto it get their register
reads (an address write followed by a read) checked:

1. Read twice. If both copies match, pass the data on. This is the normal case: 2 transfers.
2. If they differ, read a third time and use the value that appears twice (2 out of 3).
3. If all three differ, return `ERROR_CRC`. The driver reports a communication failure instead of a wrong
   value.

Plain reads and plain writes pass through unchanged.

## Configuration

```yaml
i2c:
  - id: bus_a
    sda: GPIO9
    scl: GPIO10

i2c_vote:
  - id: bus_a_vote
    i2c_id: bus_a
    update_interval: 60s   # how often the diagnostic counters are published

ads1115:
  - id: ads_48
    i2c_id: bus_a_vote     # only this device goes through the vote
    address: 0x48

sensor:
  - platform: i2c_vote
    i2c_vote_id: bus_a_vote
    corrected:
      name: "I2C corrected reads"   # first two copies differed, the third settled it
    rejected:
      name: "I2C rejected reads"    # all three copies differed
```

Once a vote bus exists there are two I2C buses, so every I2C device in the configuration needs an explicit
`i2c_id`.

## Limits

- **Only devices whose register reads have no side effects.** Repeating a read must return the same data.
  Do not use it for FIFOs, for measurement results that need a new command before each read, or for flags
  that are cleared on read. Plain reads are never repeated, but a device that clears a flag on a register
  read would lose it.
- **Registers that change on their own** between two reads (counters, a free-running conversion register in
  continuous mode) cause extra third reads and, if they change twice, rejected reads.
- **Writes are not checked.** A corrupted write to a configuration register is not detected.
- **Errors that repeat in the same way** (for example a bit that is always read wrong) are not detected.
  Voting only catches random errors.
- **Reads longer than 32 bytes** are passed through without voting.
- **Cost:** twice the bus time for register reads, 64 bytes of stack during a vote, two counters per vote
  bus. Devices not on a vote bus pay nothing.

## Statistical analysis

Notation:

- `p`: probability that one read (one copy) is corrupted;
- `q`: probability that two corrupted copies are identical. It is not small: some patterns, such as
  `0xE3E3`, repeat regardless of the true value.

Errors are assumed to be independent (see the last part of this section). Example values: `p = 2·10⁻³`
(measured on an ADS1015 behind a poor connector: about 1 corrupted read in 800), `q` between 0.02 and 0.2.

### Measurements whose first copy is corrupted

Without voting, these would reach the user as wrong values.

| Outcome | Formula | q = 0.02 | q = 0.2 |
|---|---|---|---|
| Corrected | ≈ 1 − 2p | 99.6 % | 99.6 % |
| Rejected (error reported) | ≈ 2p(1 − q) | 0.39 % | 0.32 % |
| Passed on with a wrong value | ≈ 2pq | 0.008 % | 0.08 % |

An error is not corrected only when a second copy in the same vote is also corrupted. In that case the
read is almost always rejected. A wrong value passes only when two corrupted copies are identical.

### Measurements whose first copy is correct

| Outcome | Formula | Value |
|---|---|---|
| Passed on unchanged | 1 − p² | 99.9996 % |
| Rejected although the first copy was correct | p²(1 − q) | ≈ 4·10⁻⁶ |
| Replaced by a wrong value | p²q | 8·10⁻⁸ … 8·10⁻⁷ |

A correct read is lost only when both other copies are corrupted.

### Overall

| | Without voting | With voting |
|---|---|---|
| Wrong values passed on, per read | p = 2·10⁻³ | ≈ 3p²q = 2.4·10⁻⁷ … 2.4·10⁻⁶ |
| Rejected reads, per read | 0 | ≈ 3p²(1 − q) ≈ 1·10⁻⁵ |
| Transfers per read | 1 | ≈ 2 + 2p |

For example, a device with 4 channels read every 15 s (about 23 000 reads a day) goes from about 46 wrong
values a day to between one every half year and one every 18 days, at the cost of about one rejected read
every 4 to 5 days.

### When errors come in bursts

The numbers above assume that a corrupted copy does not make the next copy more likely to be corrupted. A
poor contact can break this: if, after a corrupted copy, the next one is corrupted with probability `c`,
then about `c` of the corrupted measurements are not corrected (instead of `2p`).

The diagnostic counters show which case applies. With independent errors:

```
rejected / corrected ≈ 1.5 · p · (1 − q)     (about 0.003 for p = 2·10⁻³)
```

A ratio clearly above this means the errors come in bursts. A short pause between the copies (a few
milliseconds) would then help, because the copies would no longer hit the same disturbance.
