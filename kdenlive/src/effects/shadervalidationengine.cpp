/*
    SPDX-FileCopyrightText: 2026 VidMate AI Team
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "shadervalidationengine.h"
#include <QDebug>
#include <QRegularExpression>
#include <QGuiApplication>

static ShaderValidationEngine *s_instance = nullptr;

ShaderValidationEngine *ShaderValidationEngine::instance()
{
    if (!s_instance) {
        s_instance = new ShaderValidationEngine(qApp);
    }
    return s_instance;
}

ShaderValidationEngine::ShaderValidationEngine(QObject *parent)
    : QObject(parent)
{
}

ShaderValidationEngine::~ShaderValidationEngine()
{
    if (m_surface) {
        m_surface->destroy();
        delete m_surface;
    }
    if (m_ownsContext && m_context) {
        delete m_context;
    }
}

bool ShaderValidationEngine::ensureContext()
{
    if (m_context && m_context->isValid() && m_surface && m_surface->isValid()) {
        return true;
    }

    if (!m_context) {
        m_context = new QOpenGLContext(this);
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 2);
        format.setProfile(QSurfaceFormat::CoreProfile);
        m_context->setFormat(format);
        if (!m_context->create()) {
            qWarning() << "ShaderValidationEngine: Failed to create offscreen QOpenGLContext";
            return false;
        }
        m_ownsContext = true;
    }

    if (!m_surface) {
        m_surface = new QOffscreenSurface();
        m_surface->setFormat(m_context->format());
        m_surface->create();
    }

    return m_surface && m_surface->isValid() && m_context && m_context->isValid();
}

bool ShaderValidationEngine::makeCurrent()
{
    if (!ensureContext()) {
        return false;
    }
    return m_context->makeCurrent(m_surface);
}

void ShaderValidationEngine::doneCurrent()
{
    if (m_context) {
        m_context->doneCurrent();
    }
}

QString ShaderValidationEngine::standardVertexShader()
{
    return QStringLiteral(
        "#version 150\n"
        "in vec2 in_position;\n"
        "in vec2 in_texCoord;\n"
        "out vec2 fragCoord;\n"
        "out vec2 v_texCoord;\n"
        "void main() {\n"
        "    v_texCoord = in_texCoord;\n"
        "    fragCoord = in_position;\n"
        "    gl_Position = vec4(in_position, 0.0, 1.0);\n"
        "}\n"
    );
}

QString ShaderValidationEngine::standardShaderToyTemplate()
{
    return QStringLiteral(
        "#version 150\n"
        "#ifdef GL_ES\n"
        "precision highp float;\n"
        "#endif\n"
        "\n"
        "// Standard ShaderToy & VidMate Video Timeline Uniforms\n"
        "uniform vec3      iResolution;           // viewport resolution (in pixels)\n"
        "uniform float     iTime;                 // shader playback time (in seconds)\n"
        "uniform float     iTimeDelta;            // render time (in seconds)\n"
        "uniform float     iFrameRate;            // shader frame rate\n"
        "uniform int       iFrame;                // shader playback frame\n"
        "uniform vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click\n"
        "uniform vec4      iDate;                 // (year, month, day, time in seconds)\n"
        "uniform float     iSampleRate;           // sound sample rate (i.e., 44100)\n"
        "uniform float     iAudioLevels;          // live audio RMS / peak level [0.0 - 1.0]\n"
        "\n"
        "// Procedural AI & Keyframe Animation Parameters\n"
        "uniform float     iCustomParam1;         // User/AI parameter 1 (e.g. speed, wave frequency)\n"
        "uniform float     iCustomParam2;         // User/AI parameter 2 (e.g. distortion, viscosity)\n"
        "uniform float     iCustomParam3;         // User/AI parameter 3 (e.g. glow, roughness)\n"
        "uniform float     iCustomParam4;         // User/AI parameter 4 (e.g. scale, zoom)\n"
        "uniform float     iCustomParam5;         // User/AI parameter 5\n"
        "uniform float     iCustomParam6;         // User/AI parameter 6\n"
        "uniform float     iCustomParam7;         // User/AI parameter 7\n"
        "uniform float     iCustomParam8;         // User/AI parameter 8\n"
        "uniform vec4      iColor1;               // Primary tint / palette color\n"
        "uniform vec4      iColor2;               // Secondary tint / palette color\n"
        "\n"
        "// Video Input Channels (Timeline Clips & Node Buffers)\n"
        "uniform sampler2D iChannel0;             // Video track / node input 0\n"
        "uniform sampler2D iChannel1;             // Video track / node input 1\n"
        "uniform sampler2D iChannel2;             // Video track / node input 2\n"
        "uniform sampler2D iChannel3;             // Video track / node input 3\n"
        "\n"
        "in vec2 fragCoord;\n"
        "in vec2 v_texCoord;\n"
        "out vec4 fragColor;\n"
        "\n"
        "// =================== AI / USER SHADER CODE START ===================\n"
        "[AI_GENERATED_MATH_INSERTION_POINT]\n"
        "// ==================== AI / USER SHADER CODE END ====================\n"
        "\n"
        "void main() {\n"
        "    vec2 pixelCoords = gl_FragCoord.xy;\n"
        "    vec4 outCol = vec4(0.0);\n"
        "    mainImage(outCol, pixelCoords);\n"
        "    fragColor = outCol;\n"
        "}\n"
    );
}

QString ShaderValidationEngine::standardRaymarchTemplate()
{
    return QStringLiteral(
        "#version 150\n"
        "#ifdef GL_ES\n"
        "precision highp float;\n"
        "#endif\n"
        "\n"
        "uniform vec3      iResolution;\n"
        "uniform float     iTime;\n"
        "uniform float     iAudioLevels;\n"
        "uniform float     iCustomParam1;\n"
        "uniform float     iCustomParam2;\n"
        "uniform float     iCustomParam3;\n"
        "uniform float     iCustomParam4;\n"
        "uniform vec4      iColor1;\n"
        "uniform vec4      iColor2;\n"
        "uniform sampler2D iChannel0;\n"
        "\n"
        "in vec2 fragCoord;\n"
        "in vec2 v_texCoord;\n"
        "out vec4 fragColor;\n"
        "\n"
        "// Forward declaration of user SDF map function\n"
        "float map(vec3 p);\n"
        "vec3 computeMaterial(vec3 p, vec3 n);\n"
        "\n"
        "// =================== AI / USER SDF MATH START ===================\n"
        "[AI_GENERATED_MATH_INSERTION_POINT]\n"
        "// ==================== AI / USER SDF MATH END ====================\n"
        "\n"
        "vec3 calcNormal(vec3 p) {\n"
        "    const float h = 0.0005;\n"
        "    const vec2 k = vec2(1.0, -1.0);\n"
        "    return normalize(k.xyy * map(p + k.xyy * h) +\n"
        "                     k.yyx * map(p + k.yyx * h) +\n"
        "                     k.yxy * map(p + k.yxy * h) +\n"
        "                     k.xxx * map(p + k.xxx * h));\n"
        "}\n"
        "\n"
        "void main() {\n"
        "    vec2 uv = (gl_FragCoord.xy - 0.5 * iResolution.xy) / iResolution.y;\n"
        "    vec3 ro = vec3(0.0, 0.0, 3.0);\n"
        "    vec3 rd = normalize(vec3(uv, -1.5));\n"
        "    float t = 0.0;\n"
        "    for(int i = 0; i < 100; i++) {\n"
        "        vec3 p = ro + rd * t;\n"
        "        float d = map(p);\n"
        "        if(d < 0.001 || t > 20.0) break;\n"
        "        t += d;\n"
        "    }\n"
        "    vec3 col = vec3(0.0);\n"
        "    if(t < 20.0) {\n"
        "        vec3 p = ro + rd * t;\n"
        "        vec3 n = calcNormal(p);\n"
        "        col = computeMaterial(p, n);\n"
        "    }\n"
        "    fragColor = vec4(col, 1.0);\n"
        "}\n"
    );
}

QString ShaderValidationEngine::wrapShaderSource(const QString &glslSnippet, ShaderFormat format) const
{
    QString trimmed = glslSnippet.trimmed();

    // If the snippet is already a full standalone fragment shader with #version and main()
    if (trimmed.contains(QLatin1String("#version")) && trimmed.contains(QLatin1String("void main("))) {
        return trimmed;
    }

    QString templateStr;
    switch (format) {
    case RaymarchSDF:
        templateStr = standardRaymarchTemplate();
        break;
    case RawFragment:
        templateStr = QStringLiteral(
            "#version 150\n"
            "uniform vec3 iResolution;\n"
            "uniform float iTime;\n"
            "in vec2 fragCoord;\n"
            "out vec4 fragColor;\n"
            "[AI_GENERATED_MATH_INSERTION_POINT]\n"
        );
        break;
    case ShaderToy:
    default:
        templateStr = standardShaderToyTemplate();
        break;
    }

    return templateStr.replace(QStringLiteral("[AI_GENERATED_MATH_INSERTION_POINT]"), trimmed);
}

ShaderValidationEngine::ValidationResult ShaderValidationEngine::validateShader(const QString &aiGlslCode, ShaderFormat format)
{
    QMutexLocker locker(&m_mutex);
    ValidationResult res;
    res.finalGlslCode = wrapShaderSource(aiGlslCode, format);

    if (!ensureContext()) {
        res.success = false;
        res.errorLog = QStringLiteral("Failed to initialize offscreen OpenGL context for validation.");
        return res;
    }

    m_context->makeCurrent(m_surface);

    QOpenGLShaderProgram testProgram;

    // 1. Compile Vertex Shader (Static boilerplate screen-quad shader)
    if (!testProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, standardVertexShader())) {
        res.success = false;
        res.errorLog = QStringLiteral("Vertex Shader Error:\n") + testProgram.log();
        m_context->doneCurrent();
        return res;
    }

    // 2. Compile Fragment Shader (The AI/User generated mathematical pipeline)
    if (!testProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, res.finalGlslCode)) {
        res.success = false;
        res.errorLog = testProgram.log();

        // Extract line number if reported by driver: "0:42: error: ..." or "ERROR: 0:42: ..."
        static const QRegularExpression lineRegex(QStringLiteral(R"((\d+):(\d+):)"));
        auto match = lineRegex.match(res.errorLog);
        if (match.hasMatch()) {
            res.errorLine = match.captured(2).toInt();
        }

        m_context->doneCurrent();
        return res;
    }

    // 3. Link Program to verify uniform bindings & function signatures
    if (!testProgram.link()) {
        res.success = false;
        res.errorLog = QStringLiteral("Shader Link Error:\n") + testProgram.log();
        m_context->doneCurrent();
        return res;
    }

    // Detect active uniforms used in the shader
    static const QStringList possibleUniforms = {
        QStringLiteral("iTime"), QStringLiteral("iResolution"), QStringLiteral("iMouse"),
        QStringLiteral("iAudioLevels"), QStringLiteral("iCustomParam1"), QStringLiteral("iCustomParam2"),
        QStringLiteral("iCustomParam3"), QStringLiteral("iCustomParam4"), QStringLiteral("iColor1"),
        QStringLiteral("iColor2"), QStringLiteral("iChannel0"), QStringLiteral("iChannel1")
    };
    for (const auto &u : possibleUniforms) {
        if (testProgram.uniformLocation(u) != -1) {
            res.detectedUniforms.append(u);
        }
    }

    m_context->doneCurrent();
    res.success = true;
    return res;
}
