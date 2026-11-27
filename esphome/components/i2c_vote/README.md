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
- a repeated byte, such as `0xE3E3` or `0x4A4A`: the device sent the same byte twice;
- zeros, `0x0000`, on several reads in a row: after the glitch the device kept the data line low for a few
  transfers.

## How it works

`i2c_vote` is a virtual bus, like a `tca9548a` channel. Devices that are moved onto it get their register
reads (an address write followed by a read) checked:

1. Read twice. If both copies match, pass the data on. This is the normal case: 2 transfers.
2. If they differ, read a third time. If the third copy matches the first, pass it on: the second copy was
   a single bad read.
3. Otherwise return `ERROR_CRC`. The driver reports a communication failure instead of a wrong value.

The second and third copies are not allowed to outvote the first. A glitch can leave the device returning
the same wrong bytes for a few transfers in a row, so two copies read right after a disturbance can agree on
a wrong value. This was seen on hardware: copies `00B3 0000 0000`, where an earlier version of this
component (plain 2 out of 3) passed on `0x0000`. The first copy, read before the disturbance, is the one to
confirm.

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
      name: "I2C corrected reads"   # second copy differed, the third matched the first
    rejected:
      name: "I2C rejected reads"    # first and third copies differed
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
  Voting only catches random errors. A device that is already stuck before the first copy, and returns the
  same wrong bytes on the first two copies, is not detected either.
- **Reads longer than 32 bytes** are passed through without voting.
- **Cost:** twice the bus time for register reads, 64 bytes of stack during a vote, two counters per vote
  bus. Devices not on a vote bus pay nothing.

## Statistical analysis

Notation:

- `p`: probability that one read (one copy) is corrupted;
- `q`: probability that two corrupted copies are identical. It is not small: some patterns, such as
  `0xE3E3`, repeat regardless of the true value;
- `c`: probability that the copy right after a corrupted copy is also corrupted (errors in bursts). With
  independent errors `c = p`.

Example values: `p = 2·10⁻³` (measured on an ADS1015 behind a poor connector: about 1 corrupted read in 800),
`q` between 0.02 and 0.2.

### Independent errors (`c = p`)

Measurements whose first copy is corrupted (without voting, these would reach the user as wrong values):

| Outcome | Formula | Value |
|---|---|---|
| Rejected (error reported) | ≈ 1 | ≈ 100 % |
| Passed on with a wrong value | ≈ pq | 0.004 % … 0.04 % |

The first copy is never replaced, so a bad first copy is always rejected, unless the second copy is
corrupted in exactly the same way.

Measurements whose first copy is correct:

| Outcome | Formula | Value |
|---|---|---|
| Passed on unchanged (second copy good, or third copy good) | ≈ 1 − p² | 99.9996 % |
| Rejected although the first copy was correct | ≈ p² | ≈ 4·10⁻⁶ |
| Replaced by a wrong value | 0 | the first copy is never replaced |

Overall:

| | Without voting | With voting |
|---|---|---|
| Wrong values passed on, per read | p = 2·10⁻³ | ≈ p²q = 8·10⁻⁸ … 8·10⁻⁷ |
| Rejected reads, per read | 0 | ≈ p ≈ 2·10⁻³ |
| Corrected reads, per read | 0 | ≈ p (bad second copy, good third) |
| Transfers per read | 1 | ≈ 2 + 2p |

Voting turns almost every wrong value into a missing value. For a device with 4 channels read every 15 s
(about 23 000 reads a day) this is about 46 wrong values a day without voting, against about 46 missing
values a day and one wrong value every few months to few years with voting.

### Errors in bursts (`c` close to 1)

If the device stays stuck for a few transfers after a glitch, the copies after a corrupted one are also
corrupted, often identically (`0x0000`, `0xFFFF`). Then:

- a bad first copy is followed by bad second and third copies, which may agree with each other. A plain
  2 out of 3 vote would pass that value on; this component rejects it;
- a bad second copy is usually followed by a bad third copy, so the read is rejected instead of corrected;
- a wrong value is passed on only when the device was already stuck during the first copy and returned the
  same bytes on the first two copies.

The diagnostic counters show which case applies:

```
corrected / rejected ≈ 1        independent errors (a bad second copy is as likely as a bad first one,
                                and the third copy is then good)
corrected / rejected ≪ 1        bursts: after a bad copy the next one is bad too
```

### Measured

ADS1015 behind a poor connector, 4 channels every 15 s, 400 kHz, first version of this component (plain
2 out of 3), about 10 hours: 6 reads "corrected", 1 rejected. All 6 corrected reads gave `0x0000`, a wrong
value, at the moments where the measured voltage was clearly not zero. With diagnostic logging, typical
copies were `00B3 0000 0000` and `4850 FFFF 0000`. The errors come in bursts, which is why the second and
third copies are no longer allowed to outvote the first.
