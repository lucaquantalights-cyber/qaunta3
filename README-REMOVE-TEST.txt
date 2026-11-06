QUANTA PRODUCTION-MODE PATCH
============================

This patch removes the compile-time QUANTA_TEST_BUILD flag from PlatformIO.
It keeps the existing production firmware source and build process intact.

FILES
-----
platformio.ini                 Replace the file in your Quanta project root.
scripts/build_device.py        Replace the file in your Quanta project's scripts folder.

BUILD
-----
1. Extract these files over your existing Quanta project, preserving folders.
2. Open a terminal in the Quanta project root (the folder containing platformio.ini).
3. Run:
       python scripts\build_device.py
4. When asked to raise P-0.0.7 to P-0.0.8, answer Y.
5. Enter a release note such as:
       Production build - test mode removed
6. Confirm the build finishes and says it copied to:
       C:\Users\<your-user>\OneDrive\Firebase\quanta\firebase\public\quanta
7. From the Firebase project folder run:
       firebase deploy --only hosting
8. Verify https://quantalights.web.app/quanta/firmware.json reports P-0.0.8.
9. On the controller dashboard, check for the update and install P-0.0.8.

WHAT THIS REMOVES
-----------------
With QUANTA_TEST_BUILD undefined, firmware no longer:
- reports testBuild:true (so the TEST BUILD banner disappears);
- polls the cloud testReset command;
- includes the test reset wipe/restart handler;
- forcibly disables OTA automatic-update preference.

IMPORTANT
---------
This is a firmware-source change. It does not remove a separately deployed
Firebase callable function named testResetQuantaDevice. The uploaded project
ZIP did not include the Firebase functions source folder, so that server-side
function has not been changed here. The production firmware will no longer
poll or act on its testReset command.

The firmware version is raised by build_device.py so the existing P-0.0.7
controller can receive it over OTA. Do not use --no-bump for this release.
