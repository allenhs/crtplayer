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
    bool cgModels = true;
    int cgModelFinish = 0;   // picture in the theater: 0 35mm print, 1 worn 16mm, 2 drive-in, 3 keep the current look
    bool keepAwake = true;  // no screen dimming / sleep while a video plays
    bool lookSound = true;  // the look's sound: tape hiss, TV speaker, film crackle
    double noiseVolume = 1.0;     // how loud the hiss and crackle are (0 .. 2)
    double effectStrength = 1.0;  // how strongly the sound is changed (0 .. 1)
    QString hiddenPluginNotice;   // the missing-plugin list the user chose not to see again
    QStringList recentFiles;   // local files, most recent first (max 15)   // 0 CRT television, 1 flat-face CRT, 2 flat panel

    void load();
    void save() const;
    static QString defaultScreenshotDir();
};
