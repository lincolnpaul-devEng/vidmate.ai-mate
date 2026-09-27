/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Natron Script Generator — Implementation of programmatic node graph assembly
 * for headless execution with NatronRenderer.
 */

#include "natronscriptgenerator.h"
#include <QJsonArray>

static QString sanitizePath(const QString &path)
{
    QString p = path;
    return p.replace(QLatin1Char('\\'), QLatin1String("/"));
}

QString NatronScriptGenerator::generateChromaKeyScript(const QString &inputClip,
                                                       const QString &outputPath,
                                                       const QColor &keyColor,
                                                       double despill,
                                                       double feather,
                                                       int startFrame,
                                                       int endFrame)
{
    Q_UNUSED(despill)
    QString script;
    script += QStringLiteral(
        "# Auto-generated Natron Chroma Key Node Graph\n"
        "import NatronEngine\n"
        "import sys\n\n"
        "app = natron.getActiveInstance()\n\n"
        "# 1. Create Source Reader\n"
        "reader = app.createNode('fr.inria.openfx.ReadOIIO')\n"
        "reader.getParam('filename').set('%1')\n\n"
        "# 2. Create Chroma Keyer Node\n"
        "keyer = app.createNode('net.sf.openfx.KeyerPlugin')\n"
        "keyer.connectInput(0, reader)\n"
        "if keyer.getParam('keyColor'):\n"
        "    keyer.getParam('keyColor').set(%2, %3, %4, 1.0)\n\n"
        "# 3. Optional Despill / Edge Feather\n"
        "if %5 > 0.0:\n"
        "    blur = app.createNode('net.sf.cimg.CImgBlur')\n"
        "    blur.connectInput(0, keyer)\n"
        "    blur.getParam('size').set(%5, %5)\n"
        "    lastNode = blur\n"
        "else:\n"
        "    lastNode = keyer\n\n"
        "# 4. Create Destination Writer\n"
        "writer = app.createNode('fr.inria.openfx.WriteOIIO')\n"
        "writer.connectInput(0, lastNode)\n"
        "writer.getParam('filename').set('%6')\n\n"
        "# 5. Execute Headless Render\n"
        "first = %7 if %7 > 0 else (reader.getParam('firstFrame').get() if reader.getParam('firstFrame') else 1)\n"
        "last = %8 if %8 > 0 else (reader.getParam('lastFrame').get() if reader.getParam('lastFrame') else 1)\n"
        "print(f'[Natron] Rendering Chroma Key frames {first} to {last}...')\n"
        "app.render(writer, int(first), int(last))\n"
        "print('[Natron] Render complete.')\n"
    ).arg(sanitizePath(inputClip))
     .arg(keyColor.redF())
     .arg(keyColor.greenF())
     .arg(keyColor.blueF())
     .arg(feather)
     .arg(sanitizePath(outputPath))
     .arg(startFrame)
     .arg(endFrame);

    return script;
}

QString NatronScriptGenerator::generateRotoscopeScript(const QString &inputClip,
                                                      const QString &outputPath,
                                                      const QString &backgroundClip,
                                                      double feather,
                                                      int startFrame,
                                                      int endFrame)
{
    Q_UNUSED(feather)
    QString script;
    script += QStringLiteral(
        "# Auto-generated Natron Rotoscope & Matte Composite Node Graph\n"
        "import NatronEngine\n\n"
        "app = natron.getActiveInstance()\n\n"
        "# 1. Read Foreground\n"
        "fgReader = app.createNode('fr.inria.openfx.ReadOIIO')\n"
        "fgReader.getParam('filename').set('%1')\n\n"
        "# 2. Roto Node\n"
        "roto = app.createNode('fr.inria.built-in.Roto')\n"
        "roto.connectInput(0, fgReader)\n\n"
    ).arg(sanitizePath(inputClip));

    if (!backgroundClip.isEmpty()) {
        script += QStringLiteral(
            "# 3. Read Background & Merge\n"
            "bgReader = app.createNode('fr.inria.openfx.ReadOIIO')\n"
            "bgReader.getParam('filename').set('%1')\n"
            "merge = app.createNode('net.sf.openfx.MergePlugin')\n"
            "merge.connectInput(0, bgReader)  # B input (bg)\n"
            "merge.connectInput(1, fgReader)  # A input (fg)\n"
            "merge.connectInput(2, roto)      # Mask\n"
            "lastNode = merge\n"
        ).arg(sanitizePath(backgroundClip));
    } else {
        script += QStringLiteral(
            "lastNode = roto\n"
        );
    }

    script += QStringLiteral(
        "\n# 4. Writer & Execution\n"
        "writer = app.createNode('fr.inria.openfx.WriteOIIO')\n"
        "writer.connectInput(0, lastNode)\n"
        "writer.getParam('filename').set('%1')\n"
        "first = %2 if %2 > 0 else (fgReader.getParam('firstFrame').get() if fgReader.getParam('firstFrame') else 1)\n"
        "last = %3 if %3 > 0 else (fgReader.getParam('lastFrame').get() if fgReader.getParam('lastFrame') else 1)\n"
        "print(f'[Natron] Rendering Roto Composite frames {first} to {last}...')\n"
        "app.render(writer, int(first), int(last))\n"
        "print('[Natron] Render complete.')\n"
    ).arg(sanitizePath(outputPath))
     .arg(startFrame)
     .arg(endFrame);

    return script;
}

QString NatronScriptGenerator::generateTrackerTransformScript(const QString &inputClip,
                                                              const QString &overlayClip,
                                                              const QString &outputPath,
                                                              const QJsonObject &trackingKeyframes,
                                                              int startFrame,
                                                              int endFrame)
{
    Q_UNUSED(trackingKeyframes)
    QString script;
    script += QStringLiteral(
        "# Auto-generated Natron Tracker / Match-Move Node Graph\n"
        "import NatronEngine\n\n"
        "app = natron.getActiveInstance()\n\n"
        "# 1. Read Background Plate\n"
        "bg = app.createNode('fr.inria.openfx.ReadOIIO')\n"
        "bg.getParam('filename').set('%1')\n\n"
        "# 2. Read Overlay Graphic/Element\n"
        "fg = app.createNode('fr.inria.openfx.ReadOIIO')\n"
        "fg.getParam('filename').set('%2')\n\n"
        "# 3. Transform / Tracker Node\n"
        "xform = app.createNode('net.sf.openfx.TransformPlugin')\n"
        "xform.connectInput(0, fg)\n\n"
        "# 4. Composite Merge\n"
        "merge = app.createNode('net.sf.openfx.MergePlugin')\n"
        "merge.connectInput(0, bg)\n"
        "merge.connectInput(1, xform)\n\n"
        "# 5. Destination Writer\n"
        "writer = app.createNode('fr.inria.openfx.WriteOIIO')\n"
        "writer.connectInput(0, merge)\n"
        "writer.getParam('filename').set('%3')\n\n"
        "first = %4 if %4 > 0 else (bg.getParam('firstFrame').get() if bg.getParam('firstFrame') else 1)\n"
        "last = %5 if %5 > 0 else (bg.getParam('lastFrame').get() if bg.getParam('lastFrame') else 1)\n"
        "print(f'[Natron] Rendering Match-Move frames {first} to {last}...')\n"
        "app.render(writer, int(first), int(last))\n"
        "print('[Natron] Render complete.')\n"
    ).arg(sanitizePath(inputClip))
     .arg(sanitizePath(overlayClip))
     .arg(sanitizePath(outputPath))
     .arg(startFrame)
     .arg(endFrame);

    return script;
}

QString NatronScriptGenerator::generateColorGradeScript(const QString &inputClip,
                                                        const QString &outputPath,
                                                        double lift,
                                                        double gamma,
                                                        double gain,
                                                        double saturation,
                                                        int startFrame,
                                                        int endFrame)
{
    QString script;
    script += QStringLiteral(
        "# Auto-generated Natron Grade & Color Correction Node Graph\n"
        "import NatronEngine\n\n"
        "app = natron.getActiveInstance()\n\n"
        "# 1. Read Source Clip\n"
        "reader = app.createNode('fr.inria.openfx.ReadOIIO')\n"
        "reader.getParam('filename').set('%1')\n\n"
        "# 2. Grade Plugin\n"
        "grade = app.createNode('net.sf.openfx.GradePlugin')\n"
        "grade.connectInput(0, reader)\n"
        "if grade.getParam('blackPoint'):\n"
        "    grade.getParam('blackPoint').set(%2, %2, %2, 0.0)\n"
        "if grade.getParam('gamma'):\n"
        "    grade.getParam('gamma').set(%3, %3, %3, 1.0)\n"
        "if grade.getParam('gain'):\n"
        "    grade.getParam('gain').set(%4, %4, %4, 1.0)\n\n"
        "# 3. Saturation (ColorCorrect)\n"
        "cc = app.createNode('net.sf.openfx.ColorCorrect')\n"
        "cc.connectInput(0, grade)\n"
        "if cc.getParam('MasterSaturation'):\n"
        "    cc.getParam('MasterSaturation').set(%5)\n\n"
        "# 4. Destination Writer\n"
        "writer = app.createNode('fr.inria.openfx.WriteOIIO')\n"
        "writer.connectInput(0, cc)\n"
        "writer.getParam('filename').set('%6')\n\n"
        "first = %7 if %7 > 0 else (reader.getParam('firstFrame').get() if reader.getParam('firstFrame') else 1)\n"
        "last = %8 if %8 > 0 else (reader.getParam('lastFrame').get() if reader.getParam('lastFrame') else 1)\n"
        "print(f'[Natron] Rendering Color Grade frames {first} to {last}...')\n"
        "app.render(writer, int(first), int(last))\n"
        "print('[Natron] Render complete.')\n"
    ).arg(sanitizePath(inputClip))
     .arg(lift)
     .arg(gamma)
     .arg(gain)
     .arg(saturation)
     .arg(sanitizePath(outputPath))
     .arg(startFrame)
     .arg(endFrame);

    return script;
}

QString NatronScriptGenerator::generateGlitchCompositeScript(const QString &inputClip,
                                                             const QString &outputPath,
                                                             double intensity,
                                                             int startFrame,
                                                             int endFrame)
{
    QString script;
    script += QStringLiteral(
        "# Auto-generated Natron Glitch / Chromatic Aberration Pipeline\n"
        "import NatronEngine\n\n"
        "app = natron.getActiveInstance()\n\n"
        "reader = app.createNode('fr.inria.openfx.ReadOIIO')\n"
        "reader.getParam('filename').set('%1')\n\n"
        "# Split RGB channels and shift for chromatic glitch\n"
        "shuffleR = app.createNode('net.sf.openfx.ShufflePlugin')\n"
        "shuffleR.connectInput(0, reader)\n"
        "xformR = app.createNode('net.sf.openfx.TransformPlugin')\n"
        "xformR.connectInput(0, shuffleR)\n"
        "xformR.getParam('translate').set(%2 * 12.0, 0.0)\n\n"
        "merge = app.createNode('net.sf.openfx.MergePlugin')\n"
        "merge.connectInput(0, reader)\n"
        "merge.connectInput(1, xformR)\n\n"
        "writer = app.createNode('fr.inria.openfx.WriteOIIO')\n"
        "writer.connectInput(0, merge)\n"
        "writer.getParam('filename').set('%3')\n\n"
        "first = %4 if %4 > 0 else (reader.getParam('firstFrame').get() if reader.getParam('firstFrame') else 1)\n"
        "last = %5 if %5 > 0 else (reader.getParam('lastFrame').get() if reader.getParam('lastFrame') else 1)\n"
        "print(f'[Natron] Rendering Glitch Composite frames {first} to {last}...')\n"
        "app.render(writer, int(first), int(last))\n"
        "print('[Natron] Render complete.')\n"
    ).arg(sanitizePath(inputClip))
     .arg(intensity)
     .arg(sanitizePath(outputPath))
     .arg(startFrame)
     .arg(endFrame);

    return script;
}

QString NatronScriptGenerator::generateFromSpec(const QJsonObject &vfxSpec,
                                                const QString &inputClip,
                                                const QString &outputPath,
                                                int startFrame,
                                                int endFrame)
{
    QString pipeline = vfxSpec[QStringLiteral("pipeline")].toString(QStringLiteral("chroma_key"));

    if (pipeline == QStringLiteral("chroma_key")) {
        QString colorStr = vfxSpec[QStringLiteral("key_color")].toString(QStringLiteral("#00FF00"));
        QColor color(colorStr.isEmpty() ? QStringLiteral("#00FF00") : colorStr);
        double despill = vfxSpec[QStringLiteral("despill")].toDouble(0.5);
        double feather = vfxSpec[QStringLiteral("feather")].toDouble(0.0);
        return generateChromaKeyScript(inputClip, outputPath, color, despill, feather, startFrame, endFrame);
    } else if (pipeline == QStringLiteral("rotoscope")) {
        QString bgClip = vfxSpec[QStringLiteral("background_clip")].toString();
        double feather = vfxSpec[QStringLiteral("feather")].toDouble(2.0);
        return generateRotoscopeScript(inputClip, outputPath, bgClip, feather, startFrame, endFrame);
    } else if (pipeline == QStringLiteral("color_grade")) {
        double lift = vfxSpec[QStringLiteral("lift")].toDouble(0.0);
        double gamma = vfxSpec[QStringLiteral("gamma")].toDouble(1.0);
        double gain = vfxSpec[QStringLiteral("gain")].toDouble(1.0);
        double sat = vfxSpec[QStringLiteral("saturation")].toDouble(1.0);
        return generateColorGradeScript(inputClip, outputPath, lift, gamma, gain, sat, startFrame, endFrame);
    } else if (pipeline == QStringLiteral("glitch") || pipeline == QStringLiteral("glitch_composite")) {
        double intensity = vfxSpec[QStringLiteral("intensity")].toDouble(0.5);
        return generateGlitchCompositeScript(inputClip, outputPath, intensity, startFrame, endFrame);
    } else if (pipeline == QStringLiteral("tracker") || pipeline == QStringLiteral("planar_track")) {
        QString overlay = vfxSpec[QStringLiteral("overlay_clip")].toString();
        QJsonObject kfs = vfxSpec[QStringLiteral("keyframes")].toObject();
        return generateTrackerTransformScript(inputClip, overlay, outputPath, kfs, startFrame, endFrame);
    }

    // Default fallback: generic custom script wrapper
    QString customBody = vfxSpec[QStringLiteral("vfx_script")].toString();
    if (customBody.isEmpty()) {
        customBody = QStringLiteral(
            "# Generic headless Natron pipeline\n"
            "app = natron.getActiveInstance()\n"
            "reader = app.createNode('fr.inria.openfx.ReadOIIO')\n"
            "reader.getParam('filename').set('%1')\n"
            "writer = app.createNode('fr.inria.openfx.WriteOIIO')\n"
            "writer.connectInput(0, reader)\n"
            "writer.getParam('filename').set('%2')\n"
            "app.render(writer, 1, int(reader.getParam('lastFrame').get()))\n"
        ).arg(sanitizePath(inputClip), sanitizePath(outputPath));
    }

    return customBody;
}
