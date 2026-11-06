# Quanta manufacturing

Same idea as the AIDoser `manufacture_device.py`, for the Quanta Controller.

- **Quanta Test** (`quanta-test`, next to `quanta3`): test + burn-in firmware
- **quanta3**: final firmware. ONE build for every unit. Each unit's cloud identity is written into its memory
  during manufacturing and stays there when quanta3 is flashed (no per-device build like AIDoser's build_device.py).

## One-time setup
1. `pip install pyserial firebase-admin`
2. Put the Quanta Firebase service-account key at `quanta3\secrets\quanta-firebase-service-account.json`
   (Firebase console > quanta-f9c4c > Project settings > Service accounts > Generate new private key).
3. Make claim codes: `python scripts\generate_claim_batch.py`
4. The first run asks for the Firebase **Web API key** and saves it in `scripts\quanta_cloud.json`.
   Put a setup URL there too (`setupUrl`) if you want it printed on labels.

## Each unit (run from the quanta3 folder)
`python scripts\manufacture_quanta.py`

1. Uploads Quanta Test, reads MAC / chip ID / hotspot name
2. Device ID (quanta1, quanta2, ...) and label (QNT-00001 ...) into `scripts\devices.json`
3. Hardware test: pull-ups, both I2C buses, both DACs, ADC, all 8 outputs, 15 V supply, battery clock
   (have the 15 V adapter and LED test board plugged in)
4. You confirm the status LED colours and press SW1 and BOOT
5. Sets the battery clock from the PC, then the power-cut test (unplug ALL power 30 s) proves the CR2032
6. Burn-in (default 10 min)
7. Claim code, Firebase device login, `/deviceClaims`, `/claimInventory`, `/devices/<id>/info`
8. Writes the cloud identity into the board, reads it back, clears test settings
9. Uploads quanta3 and checks it reports the right DEVICE ID, FIRMWARE and MAC
10. `labels\<id>.txt` and `manufacturing_reports\<id>_<time>.json`

Any failure stops the script with the reason, and the unit's `manufactureStatus` in devices.json shows where it stopped.
Run the script again on the same board to retry; it recognises the board by its MAC.

## Two commands (run in this scripts folder)
- `python build_device.py` - build a new controller firmware version and make the update files
- `python manufacture_quanta.py` - test a board and program it
Both check the project first and fix it if a file is in the wrong place (quanta_repair.py).
The old build_device.py (8 MB layout / version file) is not used anymore.
