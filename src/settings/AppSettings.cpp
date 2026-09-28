#include "AppSettings.h"
#include <QDir>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

QString AppSettings::defaultScreenshotDir()
{
    QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pics.isEmpty()) pics = QDir::homePath();
    return QDir(pics).filePath(QStringLiteral("CRT Player"));
}

void AppSettings::load()
{
    QSettings s;
    volume = s.value("audio/volume", 0.8).toDouble();
    muted = s.value("audio/muted", false).toBool();
    hardwareDecoding = s.value("playback/hardwareDecoding", true).toBool();
    scaleMode = ScaleMode(std::clamp(s.value("display/scaleMode", int(ScaleMode::Fit)).toInt(), 0, 3));
    crop.left = s.value("display/cropLeft", 0.0).toDouble();
    crop.top = s.value("display/cropTop", 0.0).toDouble();
    crop.right = s.value("display/cropRight", 0.0).toDouble();
    crop.bottom = s.value("display/cropBottom", 0.0).toDouble();
    crop = clampCrop(crop);
    aspectOverride = s.value("display/aspectOverride", 0.0).toDouble();
    presetName = s.value("crt/preset", presetName).toString();
    const QByteArray pj = s.value("crt/params").toByteArray();
    if (!pj.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(pj);
        if (doc.isObject()) { params = CrtParams::fromJson(doc.object()); hasParams = true; }
    }
    bypass = s.value("crt/bypass", false).toBool();
    compare = s.value("crt/compare", false).toBool();
    split = s.value("crt/split", 0.5).toDouble();
    screenshotFiltered = s.value("screenshot/filtered", true).toBool();
    screenshotDir = s.value("screenshot/dir", defaultScreenshotDir()).toString();
    lastDir = s.value("ui/lastDir", QDir::homePath()).toString();
    playlist = s.value("playlist/items").toStringList();
    playlistIndex = s.value("playlist/index", -1).toInt();
    geometry = s.value("ui/geometry").toByteArray();
    windowState = s.value("ui/windowState").toByteArray();
    settingsVisible = s.value("ui/settingsVisible", false).toBool();
    playlistVisible = s.value("ui/playlistVisible", false).toBool();
    jellyfinVisible = s.value("ui/jellyfinVisible", false).toBool();
    deskYaw = s.value("desk/yaw", deskYaw).toDouble();
    deskPitch = s.value("desk/pitch", deskPitch).toDouble();
    deskHeight = s.value("desk/height", deskHeight).toDouble();
    deskCx = s.value("desk/cx", deskCx).toDouble();
    deskCy = s.value("desk/cy", deskCy).toDouble();
    deskClassic = s.value("desk/classicShape", false).toBool();
    deskOnTop = s.value("desk/keepOnTop", false).toBool();
    deskCabinet = std::clamp(s.value("desk/cabinet", 0).toInt(), 0, 6);
    recentFiles = s.value("files/recent").toStringList();
    deskRoom = s.value("desk/room", false).toBool();
    deskScene = std::clamp(s.value("scene/kind", deskRoom ? 1 : 0).toInt(), 0, 4);
    sceneWall = std::clamp(s.value("scene/wall", 0).toInt(), 0, 5);
    sceneFrameStyle = std::clamp(s.value("scene/frameStyle", 0).toInt(), 0, 2);
    sceneFrameLayout = std::clamp(s.value("scene/frameLayout", 1).toInt(), 0, 4);
    sceneFrames = s.value("scene/frames").toStringList();
    sceneTvHeight = std::clamp(s.value("scene/tvHeight", 1.7).toDouble(), 1.0, 3.0);
    scenePicHeight = std::clamp(s.value("scene/picHeight", 0.0).toDouble(), -0.6, 0.6);
    scenePicSpacing = std::clamp(s.value("scene/picSpacing", 1.0).toDouble(), 0.5, 2.0);
    scenePicSize = std::clamp(s.value("scene/picSize", 1.0).toDouble(), 0.6, 1.5);
    theaterLook = std::clamp(s.value("scene/theaterLook", 0).toInt(), 0, 3);
    arcadeArt = std::clamp(s.value("desk/arcadeArt", 0).toInt(), 0, 3);
    cgPalette = std::clamp(s.value("cg/palette", 0).toInt(), 0, 2);
    cgFloor = std::clamp(s.value("cg/floor", 0).toInt(), 0, 1);
    cgStand = std::clamp(s.value("cg/stand", 0).toInt(), 0, 2);
    cgObjects = s.value("cg/objects", true).toBool();
    cgBanding = s.value("cg/banding", false).toBool();
    cgReveal = s.value("cg/reveal", true).toBool();
    cgOrbit = s.value("cg/orbit", true).toBool();
    cgObjectSet = std::clamp(s.value("cg/objectSet", 0).toInt(), 0, 4);
    cgBackground = s.value("cg/background", true).toBool();
    cgModelsFolder = s.value("cg/modelsFolder").toString();
    cgModels = s.value("cg/models", true).toBool();
    cgModelFinish = std::clamp(s.value("cg/modelFinish", 0).toInt(), 0, 4);
    sceneMood = std::clamp(s.value("scene/mood", 0).toInt(), 0, 2);
    sceneWood = std::clamp(s.value("scene/wood", 0).toInt(), 0, 2);
    sceneFog = std::clamp(s.value("scene/fog", 0).toInt(), 0, 2);
    sceneFogStrength = std::clamp(s.value("scene/fogStrength", 1.0).toDouble(), 0.25, 2.0);
    sceneQuality = std::clamp(s.value("scene/quality", 1).toInt(), 0, 2);
    keepAwake = s.value("playback/keepAwake", true).toBool();
    lookSound = s.value("playback/lookSound", true).toBool();
    jfMaxBitrateMbps = std::clamp(s.value("jellyfin/maxBitrateMbps", 0).toInt(), 0, 1000);
    noiseVolume = std::clamp(s.value("playback/noiseVolume", 1.0).toDouble(), 0.0, 2.0);
    effectStrength = std::clamp(s.value("playback/effectStrength", 1.0).toDouble(), 0.0, 1.0);
    hiddenPluginNotice = s.value("ui/hiddenPluginNotice").toString();
}

void AppSettings::save() const
{
    QSettings s;
    s.setValue("audio/volume", volume);
    s.setValue("audio/muted", muted);
    s.setValue("playback/hardwareDecoding", hardwareDecoding);
    s.setValue("display/scaleMode", int(scaleMode));
    s.setValue("display/cropLeft", crop.left);
    s.setValue("display/cropTop", crop.top);
    s.setValue("display/cropRight", crop.right);
    s.setValue("display/cropBottom", crop.bottom);
    s.setValue("display/aspectOverride", aspectOverride);
    s.setValue("crt/preset", presetName);
    s.setValue("crt/params", QJsonDocument(params.toJson()).toJson(QJsonDocument::Compact));
    s.setValue("crt/bypass", bypass);
    s.setValue("crt/compare", compare);
    s.setValue("crt/split", split);
    s.setValue("screenshot/filtered", screenshotFiltered);
    s.setValue("screenshot/dir", screenshotDir);
    s.setValue("ui/lastDir", lastDir);
    s.setValue("playlist/items", playlist);
    s.setValue("playlist/index", playlistIndex);
    s.setValue("ui/geometry", geometry);
    s.setValue("ui/windowState", windowState);
    s.setValue("ui/settingsVisible", settingsVisible);
    s.setValue("ui/playlistVisible", playlistVisible);
    s.setValue("ui/jellyfinVisible", jellyfinVisible);
    s.setValue("desk/yaw", deskYaw);
    s.setValue("desk/pitch", deskPitch);
    s.setValue("desk/height", deskHeight);
    s.setValue("desk/cx", deskCx);
    s.setValue("desk/cy", deskCy);
    s.setValue("desk/classicShape", deskClassic);
    s.setValue("desk/keepOnTop", deskOnTop);
    s.setValue("desk/cabinet", deskCabinet);
    s.setValue("files/recent", recentFiles);
    s.setValue("desk/room", deskRoom);
    s.setValue("scene/kind", deskScene);
    s.setValue("scene/mood", sceneMood);
    s.setValue("scene/wood", sceneWood);
    s.setValue("scene/fog", sceneFog);
    s.setValue("scene/fogStrength", sceneFogStrength);
    s.setValue("scene/quality", sceneQuality);
    s.setValue("scene/wall", sceneWall);
    s.setValue("scene/frameStyle", sceneFrameStyle);
    s.setValue("scene/frameLayout", sceneFrameLayout);
    s.setValue("scene/frames", sceneFrames);
    s.setValue("scene/tvHeight", sceneTvHeight);
    s.setValue("scene/picHeight", scenePicHeight);
    s.setValue("scene/picSpacing", scenePicSpacing);
    s.setValue("scene/picSize", scenePicSize);
    s.setValue("scene/theaterLook", theaterLook);
    s.setValue("desk/arcadeArt", arcadeArt);
    s.setValue("cg/palette", cgPalette); s.setValue("cg/floor", cgFloor); s.setValue("cg/stand", cgStand);
    s.setValue("cg/objects", cgObjects); s.setValue("cg/banding", cgBanding);
    s.setValue("cg/reveal", cgReveal); s.setValue("cg/orbit", cgOrbit);
    s.setValue("cg/objectSet", cgObjectSet);
    s.setValue("cg/background", cgBackground);
    s.setValue("cg/modelsFolder", cgModelsFolder); s.setValue("cg/models", cgModels); s.setValue("cg/modelFinish", cgModelFinish);
    s.setValue("playback/keepAwake", keepAwake);
    s.setValue("playback/lookSound", lookSound);
    s.setValue("jellyfin/maxBitrateMbps", jfMaxBitrateMbps);
    s.setValue("playback/noiseVolume", noiseVolume);
    s.setValue("playback/effectStrength", effectStrength);
    s.setValue("ui/hiddenPluginNotice", hiddenPluginNotice);
    s.sync();
}
