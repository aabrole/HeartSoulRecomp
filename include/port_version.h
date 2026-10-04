#ifndef GUARD_PORT_VERSION_H
#define GUARD_PORT_VERSION_H

// The port's own version, shown on the title screen after Heart & Soul's
// ("v2.0.6 RECOMP 0.3.0", src/title_screen.c). android/app/build.gradle reads
// it from here for the app's versionName, so this is the one place to change
// it for a release. Digits, dots and the letters the title font has
// (C D E M O P R V) are drawn; anything else is left out.
#define HNS_PORT_VERSION "0.3.0"

#endif // GUARD_PORT_VERSION_H
