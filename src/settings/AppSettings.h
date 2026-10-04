#pragma once
#include "render/Geometry.h"
#include "settings/CrtParams.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

// Everything that persists between sessions (stored with QSettings in
// ~/.config/CRTPlayer/CRTPlayer.conf).
struct AppSettings {
    double volume = 0.8;
    bool muted = false;
    bool hardwareDecoding = true;
    ScaleMode scaleMode = ScaleMode::Fit;
    CropFractions crop;
    double aspectOverride = 0;
    QString presetName = QStringLiteral("Consumer Television");
    CrtParams params;            // current (possibly modified) parameters
    bool hasParams = false;
    bool bypass = false;
    bool compare = false;
    double split = 0.5;
    bool screenshotFiltered = true;
    QString screenshotDir;
    QString lastDir;
    QStringList playlist;
    int playlistIndex = -1;
    QByteArray geometry, windowState;
    bool settingsVisible = false, playlistVisible = false, jellyfinVisible = false;
    // Desk mode (3D set on the desktop)
    double deskYaw = -24, deskPitch = 9, deskHeight = 0.46, deskCx = 0.5, deskCy = 0.52;
    bool deskClassic = false, deskOnTop = false;
    int deskCabinet = 0;
    bool deskRoom = false;  // (1.9) room backdrop; migrated into deskScene
    int deskScene = 0;      // 0 your desktop, 1 desk
    int sceneMood = 0, sceneWood = 0, sceneFog = 0, sceneQuality = 1;
    double sceneFogStrength = 1.0;
    int sceneWall = 0, sceneFrameStyle = 0, sceneFrameLayout = 1;
    QStringList sceneFrames;   // up to 4 picture paths
    double sceneTvHeight = 1.7, scenePicHeight = 0.0, scenePicSpacing = 1.0, scenePicSize = 1.0;
    int theaterLook = 0;
    int arcadeArt = 0;      // arcade cabinet art: 0 space, 1 sunset, 2 neon, 3 70s woodgrain
    int cgPalette = 0, cgFloor = 0, cgStand = 0;
    bool cgObjects = true, cgBanding = false, cgReveal = true, cgOrbit = true;
    int cgObjectSet = 0;
    bool cgBackground = true;
    QString cgModelsFolder;
    int cgModelsFace = 0;    // 0 turning slowly, 1 as in the file, 2-4 turned by 90, 180, 270 degrees
    int cgModelsUp = 0;      // which way is up in your models: 0 automatic, 1 Y, 2 Z, 3 X, 4 -Y, 5 -Z, 6 -X
    bool cgModels = true;
    int cgModelFinish = 0;   // picture in the theater: 0 35mm print, 1 worn 16mm, 2 drive-in, 3 keep the current look
    bool keepAwake = true;  // no screen dimming / sleep while a video plays
    int jfMaxBitrateMbps = 0;
    int gifWidth = 480, gifFps = 15;   // GIF clips
    bool gifLook = true;               // with the CRT look (false: the original picture)   // Jellyfin quality limit, Mbit/s (0 = the original file)
    bool lookSound = true;  // the look's sound: tape hiss, TV speaker, film crackle
    double noiseVolume = 1.0;     // how loud the hiss and crackle are (0 .. 2)
    double effectStrength = 1.0;  // how strongly the sound is changed (0 .. 1)
    // 2.11: what carries over from video to video, and everyday playback
    bool subtitlesOn = false;          // subtitles are off until asked for, and then stay as set
    QString subtitleLang, audioLang;   // the languages last chosen ("en", "ja"); picked when a video offers them
    int audioDelayMs = 0;              // sound later (+) or earlier (-) than the picture
    int subSize = 1, subColor = 0, subBackground = 0, subPosition = 0;   // subtitle text
    bool nightMode = false;            // quiet parts louder, loud parts quieter
    bool deinterlace = true;           // interlaced video is deinterlaced
    bool shuffle = false;
    int repeatMode = 0;                // 0 off, 1 the whole playlist, 2 this video
    bool autoNext = false;             // at the end of the playlist, carry on with the next video in the folder
    int lookDetail = 0;                // without a graphics card, the look is drawn: 0 automatic, 1 full size, 2 half size
    bool enhanceUpscale = false;       // Enhance: sharper upscaling with effects off (graphics card)
    double enhanceSharpness = 0.5;
    bool smoothMotion = false;         // Enhance: frame generation (graphics card)
    int videoPath = 0;                 // without a graphics card: 0 the fast path when effects are off, 1 always, 2 never
    QString hiddenPluginNotice;   // the missing-plugin list the user chose not to see again
    QStringList recentFiles;   // local files, most recent first (max 15)   // 0 CRT television, 1 flat-face CRT, 2 flat panel

    void load();
    void save() const;
    static QString defaultScreenshotDir();
};
