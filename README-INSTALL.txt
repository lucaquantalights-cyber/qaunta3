QUANTA PER-DEVICE OTA PATCH
==========================

WHAT THIS CHANGES
-----------------
- build_device.py lists the completed production controllers from scripts/devices.json.
- You select quanta0, quanta1, quanta3, etc. before creating a release.
- The compiled firmware remains generic: the selected ID and device password are NOT embedded in it.
- The selected ID is recorded in that device's manifest and determines the release destination.
- Each selected release is stored only in that device's folders:
    release/quanta/device/<device-id>/firmware.bin
    release/quanta/device/<device-id>/firmware.json
    public/quanta/device/<device-id>/firmware.bin
    public/quanta/device/<device-id>/firmware.json
- The controller firmware derives its OTA folder from its locally stored cId, validates the manifest deviceId,
  and only accepts a firmware file located inside that same device folder.
- Existing saved fleet-wide /quanta/ OTA URLs are migrated to the device-specific folder on the new firmware.
- Unique local hostnames are enabled: quanta3.local, quanta4.local, etc.
- The QUANTA_TEST_BUILD compiler flag is removed from platformio.ini for production builds.

INSTALL
-------
1. Back up your current Quanta project (or commit the working tree to Git).
2. Extract this ZIP into the Quanta project root: the folder containing platformio.ini, src, lib, ui and scripts.
3. Allow it to replace the included source files. Keep your existing files that are NOT in this ZIP, especially:
     scripts/devices.json
     scripts/quanta_cloud.json
     claim_inventory/
     secrets/
     Firebase project configuration files
   The patch deliberately does not contain your device credentials or service-account key.

BUILD ONE DEVICE RELEASE
------------------------
4. From the Quanta project root, run:
     python .\scripts\build_device.py
5. Select the controller ID from the manufactured list, such as quanta3.
6. Accept the FW_VERSION bump for a new firmware code change. The current source is P-0.0.7, so the first
   new build should normally become P-0.0.8. Enter a short release note.
7. When asked for the Firebase Hosting public folder, use:
     C:\Users\lucaq\OneDrive\Firebase\quanta\firebase\public
   Press Enter if that path is already shown. The folder prompt must end in \public, not \public\quanta.
8. The builder writes only the selected device's folder. It does not modify devices.json or any other device's OTA folder.
9. From the Firebase project folder, deploy Hosting:
     firebase deploy --only hosting

IMPORTANT: FIRST UPDATE FROM THE CURRENT P-0.0.7 FIRMWARE
--------------------------------------------------------
Yes, the OTA code on the controller must be updated once. The current firmware uses the old shared manifest
URL, so before it can fetch its first per-device release you must point its existing Update server setting to
its own folder. After deploying the selected device's files, open that controller's dashboard > Settings > Firmware
and enter its per-device folder URL. For quanta3, use:
     https://quantalights.web.app/quanta/device/quanta3/
For quanta0, use .../device/quanta0/; for quanta1, use .../device/quanta1/.
Save the URL, click Check for update, then install the new firmware. The current P-0.0.7 OTA client supports
relative firmware.bin paths and ignores the additional deviceId field, so it can perform this first transition.
After the new firmware starts, it remembers/derives its own per-device URL automatically. Repeat this one-time
URL change for each existing controller before its first transition to the new OTA layout.

AFTER THE FIRST UPDATE
----------------------
Future OTA checks automatically use https://quantalights.web.app/quanta/device/<cId>/. The firmware blocks a
per-device manifest or binary whose deviceId/folder does not match that controller's stored cId.

PUBLISH THE SAME CODE TO ANOTHER DEVICE
---------------------------------------
The source firmware version is shared. After building and deploying one device release, you can publish the
same code version to another controller by running:
     python .\scripts\build_device.py --no-bump
Then select the other controller. This rebuilds the same generic firmware version and copies it to that
controller's own folder. It does not overwrite the first controller's release.

BUILD VERIFICATION
------------------
The package was checked for Python syntax and registry selection. A PlatformIO compile was not run here; run
the builder on your Windows computer to perform the actual ESP32 build before deploying.
