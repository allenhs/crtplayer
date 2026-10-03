#include "CrtPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include "WheelGuard.h"

static constexpr int kSteps = 1000;
static constexpr int kLabelWidth = 118;   // shared label column so sliders line up across groups
static QLabel* fixedLabel(const QString& t) { auto* l = new QLabel(t); l->setFixedWidth(kLabelWidth); return l; }

CrtPanel::CrtPanel(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);
    auto* body = new QWidget;
    scroll->setWidget(body);
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(14, 10, 14, 14);
    v->setSpacing(6);

    // Presets
    auto* presetBox = new QGroupBox(tr("Preset"), body);
    auto* pv = new QVBoxLayout(presetBox);
    pv->setContentsMargins(0, 6, 0, 0);
    m_presetCombo = new QComboBox(presetBox);
    m_presetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_presetCombo->setMinimumContentsLength(14);
    pv->addWidget(m_presetCombo);
    m_presetState = new QLabel(presetBox);
    m_presetState->setObjectName("presetState");
    pv->addWidget(m_presetState);
    auto* grid = new QGridLayout();
    grid->setSpacing(6);
    m_saveAs = new QPushButton(tr("Save as new…"));
    m_save = new QPushButton(tr("Save changes"));
    m_revert = new QPushButton(tr("Revert"));
    m_rename = new QPushButton(tr("Rename…"));
    m_delete = new QPushButton(tr("Delete"));
    m_import = new QPushButton(tr("Import…"));
    m_export = new QPushButton(tr("Export…"));
    m_save->setToolTip(tr("Overwrite the selected user preset with the current settings"));
    m_revert->setToolTip(tr("Discard changes and reload the selected preset"));
    m_rename->setToolTip(tr("Built-in presets cannot be renamed; save a copy first"));
    m_delete->setToolTip(tr("Built-in presets cannot be deleted"));
    grid->addWidget(m_saveAs, 0, 0);
    grid->addWidget(m_save, 0, 1);
    grid->addWidget(m_revert, 0, 2);
    grid->addWidget(m_rename, 1, 0);
    grid->addWidget(m_delete, 1, 1);
    grid->addWidget(m_import, 2, 0);
    grid->addWidget(m_export, 2, 1);
    for (QPushButton* b : {m_saveAs, m_save, m_revert, m_rename, m_delete, m_import, m_export})
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    pv->addLayout(grid);
    v->addWidget(presetBox);

    auto* toggles = new QHBoxLayout();
    m_bypass = new QPushButton(tr("Bypass effects"));
    m_bypass->setCheckable(true);
    m_bypass->setToolTip(tr("Show the plain video (B)"));
    m_compare = new QPushButton(tr("Before / after"));
    m_compare->setCheckable(true);
    m_compare->setToolTip(tr("Split view: original left, CRT right (C). Drag the divider on the video."));
    toggles->addWidget(m_bypass);
    toggles->addWidget(m_compare);
    v->addLayout(toggles);

    connect(m_presetCombo, &QComboBox::activated, this, [this](int i) { emit presetSelected(m_presetCombo->itemText(i)); });
    connect(m_saveAs, &QPushButton::clicked, this, &CrtPanel::saveAsRequested);
    connect(m_save, &QPushButton::clicked, this, &CrtPanel::saveRequested);
    connect(m_revert, &QPushButton::clicked, this, &CrtPanel::revertRequested);
    connect(m_rename, &QPushButton::clicked, this, &CrtPanel::renameRequested);
    connect(m_delete, &QPushButton::clicked, this, &CrtPanel::deleteRequested);
    connect(m_import, &QPushButton::clicked, this, &CrtPanel::importRequested);
    connect(m_export, &QPushButton::clicked, this, &CrtPanel::exportRequested);

    // Resolution ("bigger pixels")
    {
        auto* box = new QGroupBox(tr("Resolution"), body);
        auto* form = new QFormLayout(box);
        form->setContentsMargins(0, 6, 0, 0);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(7);
        m_resPreset = new QComboBox;
        m_resPreset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_resPreset->setMinimumContentsLength(10);
        const struct { const char* label; int rows; int cols; } choices[] = {
            {"Native", 0, 0}, {"480 lines", 480, 0}, {"360 lines", 360, 0}, {"288 lines (PAL)", 288, 0},
            {"240 lines (NTSC consoles)", 240, 0}, {"224 lines", 224, 0}, {"192 lines", 192, 0},
            {"160 lines", 160, 0}, {"144 lines", 144, 0}, {"120 lines", 120, 0}, {"96 lines", 96, 0},
            {"256 × 224 (16-bit console)", 224, 256}, {"320 × 240 (32-bit console)", 240, 320},
            {"320 × 200 (home computer)", 200, 320}, {"160 × 144 (handheld)", 144, 160},
        };
        for (const auto& c : choices) m_resPreset->addItem(tr(c.label), QPoint(c.cols, c.rows));
        m_resPreset->addItem(tr("Custom"), QPoint(-1, -1));
        m_resPreset->setToolTip(tr("Renders the picture at a lower resolution so each pixel becomes a visible block.\n"
                                   "Automatic scanlines then match one line per pixel row."));
        form->addRow(fixedLabel(tr("Resolution")), m_resPreset);
        m_resRows = new QSpinBox;
        m_resRows->setRange(0, 2160);
        m_resRows->setSpecialValueText(tr("Native"));
        m_resRows->setSuffix(tr(" rows"));
        m_resRows->setToolTip(tr("Rows of pixels down the picture (portrait video counts along its height)"));
        form->addRow(fixedLabel(tr("Rows")), m_resRows);
        m_resCols = new QSpinBox;
        m_resCols->setRange(0, 3840);
        m_resCols->setSpecialValueText(tr("Auto (square pixels)"));
        m_resCols->setSuffix(tr(" columns"));
        m_resCols->setToolTip(tr("Auto keeps pixels square. A fixed number gives wide or narrow pixels,\n"
                                 "e.g. 256 columns for 16-bit-console-style wide pixels. The picture's shape never changes."));
        form->addRow(fixedLabel(tr("Columns")), m_resCols);
        m_pixFilter = new QComboBox;
        m_pixFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_pixFilter->setMinimumContentsLength(10);
        m_pixFilter->addItems(pixelFilterNames());
        m_pixFilter->setToolTip(tr("Hard: pure blocks. Sharp: blocks with one-pixel smoothed edges (no shimmer when scaling).\n"
                                   "Soft: smooth interpolation, like a blurry analog source."));
        form->addRow(fixedLabel(tr("Pixel filter")), m_pixFilter);
        m_colorDepth = new QComboBox;
        m_colorDepth->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_colorDepth->setMinimumContentsLength(10);
        m_colorDepth->addItems(colorDepthNames());
        m_colorDepth->setToolTip(tr("Limits the colours like old consoles and computers: bits per channel, or a fixed palette."));
        form->addRow(fixedLabel(tr("Colours")), m_colorDepth);
        m_dither = new QComboBox;
        m_dither->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_dither->setMinimumContentsLength(10);
        m_dither->addItems(ditherNames());
        m_dither->setToolTip(tr("Ordered dithering hides banding with a fixed pattern, aligned to the picture's pixels."));
        form->addRow(fixedLabel(tr("Dither")), m_dither);
        v->addWidget(box);
        connect(m_colorDepth, &QComboBox::currentIndexChanged, this, [this](int i) { if (!m_updating) { m_params.colorDepth = i; emitChanged(); } });
        connect(m_dither, &QComboBox::currentIndexChanged, this, [this](int i) { if (!m_updating) { m_params.dither = i; emitChanged(); } });

        connect(m_resPreset, &QComboBox::activated, this, [this](int i) {
            const QPoint rc = m_resPreset->itemData(i).toPoint();
            if (rc.x() < 0) return;   // Custom: keep the numbers, edit them below
            m_params.pixelHeight = rc.y();
            m_params.pixelWidth = rc.x();
            m_updating = true;
            m_resRows->setValue(rc.y());
            m_resCols->setValue(rc.x());
            m_updating = false;
            emitChanged();
        });
        connect(m_resRows, &QSpinBox::valueChanged, this, [this](int v) {
            if (m_updating) return;
            m_params.pixelHeight = v;
            syncResPreset();
            emitChanged();
        });
        connect(m_resCols, &QSpinBox::valueChanged, this, [this](int v) {
            if (m_updating) return;
            m_params.pixelWidth = v;
            syncResPreset();
            emitChanged();
        });
        connect(m_pixFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
            if (m_updating) return;
            m_params.pixelFilter = i;
            emitChanged();
        });
    }

    // Parameter groups in table order
    QHash<QString, QFormLayout*> groups;
    auto groupLayout = [&](const QString& name) {
        if (auto* f = groups.value(name)) return f;
        auto* box = new QGroupBox(QString(name).replace('&', QStringLiteral("&&")), body);   // '&' is a mnemonic marker
        auto* form = new QFormLayout(box);
        form->setContentsMargins(0, 6, 0, 0);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(7);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        v->addWidget(box);
        groups.insert(name, form);
        return form;
    };

    for (const ParamDesc& d : floatParamDescs()) {
        QFormLayout* form = groupLayout(QString::fromUtf8(d.group));
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* s = new QSlider(Qt::Horizontal, row);
        s->setRange(0, kSteps);
        s->setToolTip(QString::fromUtf8(d.tooltip));
        auto* val = new QLabel(row);
        val->setObjectName("paramValue");
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(s, 1);
        h->addWidget(val);
        auto* label = new QLabel(QString::fromUtf8(d.label));
        label->setToolTip(QString::fromUtf8(d.tooltip));
        label->setFixedWidth(kLabelWidth);
        label->setWordWrap(true);
        form->addRow(label, row);
        const int idx = m_rows.size();
        m_rows.push_back({s, val, &d});
        connect(s, &QSlider::valueChanged, this, [this, idx](int pos) {
            const Row& r = m_rows[idx];
            const float value = r.desc->min + (r.desc->max - r.desc->min) * pos / float(kSteps);
            r.value->setText(QString::number(value, 'f', r.desc->decimals));
            if (m_updating) return;
            m_params.*(r.desc->member) = value;
            emitChanged();
        });

        if (QString::fromUtf8(d.key) == "fmvBlocks") {
            // The FMV console's switches sit above its sliders.
            m_fmvMode = new QComboBox;
            m_fmvMode->addItems(fmvModeNames());
            m_fmvMode->setToolTip(tr("Shows the video as an early-90s CD console played it. Sega CD: blocky 4×4 codec, a few dozen\n"
                                     "dithered colours from the console's 512, a low frame rate. It uses the Resolution above\n"
                                     "(224 rows with the console's wide pixels when that is left at Native)."));
            m_fmvColors = new QSpinBox;
            m_fmvColors->setRange(8, 256);
            m_fmvColors->setSuffix(tr(" colours"));
            m_fmvColors->setToolTip(tr("Colours on screen at once, picked afresh for every frame. The console managed about 64;\n"
                                       "16 is harsher, 128 or more is cleaner than it ever was."));
            m_fmvFps = new QComboBox;
            m_fmvFps->addItem(tr("The video's own"), 0);
            for (int f : {10, 12, 15, 20, 24, 30}) m_fmvFps->addItem(tr("%1 frames a second").arg(f), f);
            m_fmvFps->setToolTip(tr("How often the picture changes. Sega CD video ran at about 12 to 15 frames a second."));
            form->insertRow(0, fixedLabel(tr("Console")), m_fmvMode);
            form->insertRow(1, fixedLabel(tr("Colours")), m_fmvColors);
            form->insertRow(2, fixedLabel(tr("Frame rate")), m_fmvFps);
            connect(m_fmvMode, &QComboBox::currentIndexChanged, this, [this](int i) { if (!m_updating) { m_params.fmvMode = i; emitChanged(); } });
            connect(m_fmvColors, &QSpinBox::valueChanged, this, [this](int n) { if (!m_updating) { m_params.fmvColors = n; emitChanged(); } });
            connect(m_fmvFps, &QComboBox::currentIndexChanged, this, [this](int) {
                if (!m_updating) { m_params.fmvFps = m_fmvFps->currentData().toInt(); emitChanged(); }
            });
        }
        if (QString::fromUtf8(d.key) == "scanWidth") {
            m_lines = new QSpinBox;
            m_lines->setRange(0, 2160);
            m_lines->setSingleStep(8);
            m_lines->setSpecialValueText(tr("Auto (from video)"));
            m_lines->setSuffix(tr(" lines"));
            m_lines->setToolTip(tr("Number of simulated scanlines across the picture. Auto uses the visible video lines, "
                                   "halved until each line is at least 2.5 screen pixels tall."));
            form->addRow(fixedLabel(tr("Line count")), m_lines);
            connect(m_lines, &QSpinBox::valueChanged, this, [this](int n) {
                if (m_updating) return;
                m_params.scanLines = n;
                emitChanged();
            });
        }
        if (QString::fromUtf8(d.key) == "chroma") {
            m_videoStd = new QComboBox;
            m_videoStd->addItems({tr("NTSC (480 lines, 60 Hz)"), tr("PAL (576 lines, 50 Hz)")});
            m_videoStd->setToolTip(tr("PAL changes the tape-artefact scale, the field rate and the composite pattern "
                                      "(diagonal crawl, Hanover bars)."));
            form->insertRow(0, fixedLabel(tr("Standard")), m_videoStd);
            connect(m_videoStd, &QComboBox::currentIndexChanged, this, [this](int i) { if (!m_updating) { m_params.videoStandard = i; emitChanged(); } });
        }
        if (QString::fromUtf8(d.key) == "scanStrength") {
            m_scanType = new QComboBox;
            m_scanType->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            m_scanType->setMinimumContentsLength(10);
            m_scanType->addItems(scanTypeNames());
            m_scanType->setToolTip(tr("Soft: round Gaussian beam. Sharp: flat beam with hard gaps (studio monitors).\n"
                                      "Dynamic: thin in dark areas, fat in bright ones. Interlaced: alternating fields.\n"
                                      "VGA double-scan: every line drawn twice with a thin gap.\n"
                                      "Pixel beam: low-res pixels drawn as soft beam dots (set a line count such as 240).\n\n"
                                      "Emulator looks (independent implementations resembling well-known shaders, not ports):\n"
                                      "Geom-style: beams rebuilt from neighbouring lines in linear light, like RetroArch's crt-geom.\n"
                                      "Lottes-style: soft round pixels and beams, like crt-lottes.\n"
                                      "Easymode-style: thin dark lines that swell when bright, like crt-easymode.\n"
                                      "Hyllian-style: flat beams with hard gaps and crisp horizontals, like crt-hyllian.\n"
                                      "MAME HLSL-style: sine-shaped scanlines as in MAME's HLSL/BGFX post-processing."));
            form->insertRow(0, fixedLabel(tr("Style")), m_scanType);
            connect(m_scanType, &QComboBox::currentIndexChanged, this, [this](int i) {
                if (m_updating) return;
                m_params.scanType = i;
                emitChanged();
            });
        }
        if (QString::fromUtf8(d.key) == "maskStrength") {
            m_maskType = new QComboBox;
            m_maskType->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            m_maskType->setMinimumContentsLength(10);
            m_maskType->addItems(maskTypeNames());
            m_maskType->setToolTip(tr("Aperture grille: vertical stripes (Trinitron). Shadow mask: dot triads. Slot mask: stripes with gaps."));
            form->insertRow(0, fixedLabel(tr("Pattern")), m_maskType);
            connect(m_maskType, &QComboBox::currentIndexChanged, this, [this](int i) {
                if (m_updating) return;
                m_params.maskType = i;
                emitChanged();
            });
        }
        if (QString::fromUtf8(d.key) == "overscan") {
            m_includeBars = new QCheckBox(tr("Apply the tube to black bars too"));
            m_includeBars->setToolTip(tr("Off: letterbox/pillarbox bars stay flat and black; the tube hugs the picture.\n"
                                         "On: the whole video area becomes the tube."));
            form->addRow(fixedLabel(QString()), m_includeBars);
            connect(m_includeBars, &QCheckBox::toggled, this, [this](bool on) {
                if (m_updating) return;
                m_params.includeBars = on;
                emitChanged();
            });
        }
    }
    {
        auto* box = new QGroupBox(tr("Set behaviour"), body);
        auto* bl = new QVBoxLayout(box);
        bl->setContentsMargins(0, 6, 0, 0);
        m_power = new QCheckBox(tr("Power on/off animation"));
        m_power->setToolTip(tr("The raster opens from a bright line when playback starts, and collapses to a dot "
                               "when the playlist ends."));
        m_static = new QCheckBox(tr("Static between items"));
        m_static->setToolTip(tr("Changing to another playlist item shows static until its first picture."));
        m_vcrOsd = new QCheckBox(tr("VCR on-screen display"));
        m_vcrOsd->setToolTip(tr("PLAY, PAUSE, REW, FF and STOP in the corner, inserted into the video signal."));
        bl->addWidget(m_power);
        bl->addWidget(m_static);
        bl->addWidget(m_vcrOsd);
        v->addWidget(box);
        connect(m_power, &QCheckBox::toggled, this, [this](bool on) { if (!m_updating) { m_params.powerEffects = on; emitChanged(); } });
        connect(m_static, &QCheckBox::toggled, this, [this](bool on) { if (!m_updating) { m_params.channelStatic = on; emitChanged(); } });
        connect(m_vcrOsd, &QCheckBox::toggled, this, [this](bool on) { if (!m_updating) { m_params.vcrOsd = on; emitChanged(); } });
    }
    v->addStretch(1);
    setParams(CrtParams{});
    WheelGuard::install(this);
    // The dock must never be narrower than the controls (plus the vertical scrollbar).
    scroll->setMinimumWidth(body->minimumSizeHint().width() + 16);
}

void CrtPanel::emitChanged() { emit paramsChanged(m_params); }

void CrtPanel::syncResPreset()
{
    // Show the matching quick pick, or "Custom" for any other combination.
    QSignalBlocker b(m_resPreset);
    const QPoint want(m_params.pixelWidth, m_params.pixelHeight);
    int idx = m_resPreset->count() - 1;
    for (int i = 0; i < m_resPreset->count() - 1; ++i)
        if (m_resPreset->itemData(i).toPoint() == want) { idx = i; break; }
    m_resPreset->setCurrentIndex(idx);
}

void CrtPanel::setParams(const CrtParams& p)
{
    m_updating = true;
    m_params = p;
    for (const Row& r : m_rows) {
        const float value = p.*(r.desc->member);
        const int pos = qRound((value - r.desc->min) / (r.desc->max - r.desc->min) * kSteps);
        r.slider->setValue(pos);
        r.value->setText(QString::number(value, 'f', r.desc->decimals));
    }
    m_lines->setValue(p.scanLines);
    m_maskType->setCurrentIndex(p.maskType);
    m_scanType->setCurrentIndex(p.scanType);
    m_resRows->setValue(p.pixelHeight);
    m_resCols->setValue(p.pixelWidth);
    m_pixFilter->setCurrentIndex(p.pixelFilter);
    m_colorDepth->setCurrentIndex(p.colorDepth);
    m_dither->setCurrentIndex(p.dither);
    if (m_fmvMode) {
        m_fmvMode->setCurrentIndex(p.fmvMode);
        m_fmvColors->setValue(p.fmvColors);
        int fi = m_fmvFps->findData(p.fmvFps);
        if (fi < 0) { m_fmvFps->addItem(tr("%1 frames a second").arg(p.fmvFps), p.fmvFps); fi = m_fmvFps->count() - 1; }
        m_fmvFps->setCurrentIndex(fi);
    }
    m_videoStd->setCurrentIndex(p.videoStandard);
    m_power->setChecked(p.powerEffects);
    m_static->setChecked(p.channelStatic);
    m_vcrOsd->setChecked(p.vcrOsd);
    syncResPreset();
    m_includeBars->setChecked(p.includeBars);
    m_updating = false;
}

void CrtPanel::setPresetList(const QStringList& builtin, const QStringList& user)
{
    QSignalBlocker b(m_presetCombo);
    m_presetCombo->clear();
    m_presetCombo->addItems(builtin);
    if (!user.isEmpty()) {
        m_presetCombo->insertSeparator(m_presetCombo->count());
        m_presetCombo->addItems(user);
    }
}

void CrtPanel::setCurrentPreset(const QString& name, bool modified, bool builtin)
{
    QSignalBlocker b(m_presetCombo);
    const int i = m_presetCombo->findText(name);
    if (i >= 0) m_presetCombo->setCurrentIndex(i);
    m_presetState->setText(modified ? tr("Modified — save to keep these settings") : QString());
    m_presetState->setVisible(modified);
    m_save->setEnabled(!builtin && modified);
    m_revert->setEnabled(modified);
    m_rename->setEnabled(!builtin);
    m_delete->setEnabled(!builtin);
}
