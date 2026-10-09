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
        QStringLiteral("iCustomParam3"), QStringLiteral("iCustomParam4"), QStringLiteral("iCustomParam5"),
        QStringLiteral("iCustomParam6"), QStringLiteral("iCustomParam7"), QStringLiteral("iCustomParam8"),
        QStringLiteral("iColor1"), QStringLiteral("iColor2"),
        QStringLiteral("iChannel0"), QStringLiteral("iChannel1"), QStringLiteral("iChannel2"), QStringLiteral("iChannel3")
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

QStringList ShaderValidationEngine::availablePresetNames()
{
    return {
        // Procedural Animation
        QStringLiteral("neon_grid"),
        QStringLiteral("plasma_energy"),
        QStringLiteral("starfield_warp"),
        QStringLiteral("cyber_matrix"),
        QStringLiteral("gradient_flow"),

        // 1. Coordinate-Based Spatial Transformations
        QStringLiteral("lens_distortion"),
        QStringLiteral("chromatic_aberration"),
        QStringLiteral("fisheye"),
        QStringLiteral("wave_glitch"),
        QStringLiteral("water_ripple"),
        QStringLiteral("vortex_twist"),
        QStringLiteral("split_screen_grid"),

        // 2. Multi-Pass Rendering & Temporal Buffers
        QStringLiteral("temporal_echo"),
        QStringLiteral("motion_blur"),
        QStringLiteral("light_trails"),
        QStringLiteral("bloom_glow"),

        // 3. Multi-Texture Sampling & Advanced Blending
        QStringLiteral("displacement_mapping"),
        QStringLiteral("luminosity_blend"),

        // 4. High-Performance Color Grading & LUTs
        QStringLiteral("cinematic_teal_orange"),
        QStringLiteral("filmic_aces_tonemap"),
        QStringLiteral("chroma_keyer"),

        // 5. Kernel Convolution Filtering
        QStringLiteral("sobel_edge_sketch"),
        QStringLiteral("laplacian_edge"),
        QStringLiteral("sharpen_unsharp_mask"),
        QStringLiteral("emboss")
    };
}

QString ShaderValidationEngine::getPresetGlsl(const QString &presetName)
{
    QString p = presetName.trimmed().toLower();

    // ── 1. Coordinate-Based Spatial Transformations ──────────────────────────
    if (p == QStringLiteral("lens_distortion") || p == QStringLiteral("barrel_distortion") || p == QStringLiteral("pincushion")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec2 center = vec2(0.5, 0.5);\n"
            "    vec2 d = uv - center;\n"
            "    float r2 = dot(d, d);\n"
            "    float k = (iCustomParam1 != 0.0) ? iCustomParam1 : 0.35;\n"
            "    vec2 distUv = center + d * (1.0 + k * r2);\n"
            "    if (distUv.x < 0.0 || distUv.x > 1.0 || distUv.y < 0.0 || distUv.y > 1.0) {\n"
            "        fragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    } else {\n"
            "        fragColor = texture(iChannel0, distUv);\n"
            "    }\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("chromatic_aberration") || p == QStringLiteral("rgb_split")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec2 dir = uv - vec2(0.5);\n"
            "    float dist = length(dir);\n"
            "    float amount = (iCustomParam1 != 0.0) ? iCustomParam1 * 0.03 : 0.015;\n"
            "    vec2 offset = dir * (dist * amount);\n"
            "    float r = texture(iChannel0, uv - offset).r;\n"
            "    float g = texture(iChannel0, uv).g;\n"
            "    float b = texture(iChannel0, uv + offset).b;\n"
            "    fragColor = vec4(r, g, b, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("fisheye") || p == QStringLiteral("fish_eye")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = (fragCoord / iResolution.xy) * 2.0 - 1.0;\n"
            "    float aspect = iResolution.x / iResolution.y;\n"
            "    uv.x *= aspect;\n"
            "    float r = length(uv);\n"
            "    float strength = (iCustomParam1 != 0.0) ? iCustomParam1 : 1.25;\n"
            "    float theta = atan(uv.y, uv.x);\n"
            "    float radius = pow(r, strength);\n"
            "    vec2 fishUv = vec2(radius * cos(theta), radius * sin(theta));\n"
            "    fishUv.x /= aspect;\n"
            "    fishUv = fishUv * 0.5 + 0.5;\n"
            "    if (fishUv.x < 0.0 || fishUv.x > 1.0 || fishUv.y < 0.0 || fishUv.y > 1.0) {\n"
            "        fragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    } else {\n"
            "        fragColor = texture(iChannel0, fishUv);\n"
            "    }\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("wave_glitch") || p == QStringLiteral("glitch") || p == QStringLiteral("analog_vhs")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    float strength = (iCustomParam1 != 0.0) ? iCustomParam1 : 0.02;\n"
            "    float speed = (iCustomParam2 != 0.0) ? iCustomParam2 : 12.0;\n"
            "    float wave = sin(uv.y * 40.0 + iTime * speed) * cos(uv.y * 15.0 - iTime * speed * 0.7);\n"
            "    float glitchLine = step(0.96, fract(sin(floor(uv.y * 25.0) + floor(iTime * 10.0)) * 43758.5453));\n"
            "    vec2 distortedUv = uv + vec2(wave * strength + glitchLine * strength * 2.5, 0.0);\n"
            "    float r = texture(iChannel0, distortedUv + vec2(strength * 0.4, 0.0)).r;\n"
            "    float g = texture(iChannel0, distortedUv).g;\n"
            "    float b = texture(iChannel0, distortedUv - vec2(strength * 0.4, 0.0)).b;\n"
            "    fragColor = vec4(r, g, b, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("water_ripple") || p == QStringLiteral("ripple")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec2 c = vec2(0.5, 0.5);\n"
            "    vec2 d = uv - c;\n"
            "    float dist = length(d);\n"
            "    float speed = (iCustomParam2 != 0.0) ? iCustomParam2 : 4.0;\n"
            "    float amp = (iCustomParam1 != 0.0) ? iCustomParam1 * 0.03 : 0.015;\n"
            "    float wave = sin(dist * 50.0 - iTime * speed) * exp(-dist * 2.5);\n"
            "    vec2 rippleUv = uv + (d / max(dist, 0.0001)) * wave * amp;\n"
            "    fragColor = texture(iChannel0, rippleUv);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("vortex_twist") || p == QStringLiteral("twirl") || p == QStringLiteral("swirl")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec2 center = vec2(0.5, 0.5);\n"
            "    vec2 tc = uv - center;\n"
            "    float dist = length(tc);\n"
            "    float radius = 0.5;\n"
            "    float angle = (iCustomParam1 != 0.0) ? iCustomParam1 : 2.5;\n"
            "    if (dist < radius) {\n"
            "        float percent = (radius - dist) / radius;\n"
            "        float theta = percent * percent * angle;\n"
            "        float s = sin(theta);\n"
            "        float c = cos(theta);\n"
            "        tc = vec2(dot(tc, vec2(c, -s)), dot(tc, vec2(s, c)));\n"
            "    }\n"
            "    fragColor = texture(iChannel0, center + tc);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("split_screen_grid") || p == QStringLiteral("split_screen") || p == QStringLiteral("grid_split")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    float cells = (iCustomParam1 >= 2.0) ? floor(iCustomParam1) : 2.0;\n"
            "    vec2 cellUv = fract(uv * cells);\n"
            "    vec2 grid = step(0.015 * cells, cellUv) * (1.0 - step(1.0 - 0.015 * cells, cellUv));\n"
            "    float border = grid.x * grid.y;\n"
            "    vec4 col = texture(iChannel0, cellUv);\n"
            "    fragColor = mix(vec4(0.08, 0.08, 0.08, 1.0), col, border);\n"
            "}\n"
        );
    }

    // ── 2. Multi-Pass & Temporal Buffers ─────────────────────────────────────
    if (p == QStringLiteral("temporal_echo") || p == QStringLiteral("echo") || p == QStringLiteral("ghosting")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 current = texture(iChannel0, uv);\n"
            "    vec4 prev = texture(iChannel2, uv);\n"
            "    float decay = (iCustomParam1 > 0.0 && iCustomParam1 < 1.0) ? iCustomParam1 : 0.82;\n"
            "    fragColor = mix(current, max(current, prev * decay), 0.7);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("motion_blur") || p == QStringLiteral("frame_blend")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 current = texture(iChannel0, uv);\n"
            "    vec4 prev = texture(iChannel2, uv);\n"
            "    float blend = (iCustomParam1 > 0.0 && iCustomParam1 < 1.0) ? iCustomParam1 : 0.5;\n"
            "    fragColor = mix(current, prev, blend);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("light_trails") || p == QStringLiteral("light_streak")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 current = texture(iChannel0, uv);\n"
            "    vec4 prev = texture(iChannel2, uv);\n"
            "    float decay = (iCustomParam1 > 0.0) ? iCustomParam1 : 0.94;\n"
            "    vec3 trails = max(current.rgb, prev.rgb * decay);\n"
            "    fragColor = vec4(trails, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("bloom_glow") || p == QStringLiteral("bloom") || p == QStringLiteral("glow_filter")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 src = texture(iChannel0, uv);\n"
            "    float threshold = (iCustomParam1 != 0.0) ? iCustomParam1 : 0.65;\n"
            "    float intensity = (iCustomParam2 != 0.0) ? iCustomParam2 : 1.5;\n"
            "    vec3 glow = vec3(0.0);\n"
            "    vec2 px = 1.0 / iResolution.xy;\n"
            "    float totalWeight = 0.0;\n"
            "    for (float x = -3.0; x <= 3.0; x += 1.0) {\n"
            "        for (float y = -3.0; y <= 3.0; y += 1.0) {\n"
            "            float weight = exp(-(x * x + y * y) / 8.0);\n"
            "            vec4 s = texture(iChannel0, uv + vec2(x, y) * px * 3.5);\n"
            "            vec3 bright = max(s.rgb - vec3(threshold), vec3(0.0)) / (1.0 - threshold + 0.001);\n"
            "            glow += bright * weight;\n"
            "            totalWeight += weight;\n"
            "        }\n"
            "    }\n"
            "    glow /= totalWeight;\n"
            "    fragColor = vec4(src.rgb + glow * intensity, 1.0);\n"
            "}\n"
        );
    }

    // ── 3. Multi-Texture Sampling & Advanced Blending ────────────────────────
    if (p == QStringLiteral("displacement_mapping") || p == QStringLiteral("displacement")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 map = texture(iChannel1, uv);\n"
            "    float amount = (iCustomParam1 != 0.0) ? iCustomParam1 * 0.1 : 0.05;\n"
            "    vec2 disp = (map.rg - 0.5) * amount;\n"
            "    fragColor = texture(iChannel0, uv + disp);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("luminosity_blend") || p == QStringLiteral("luma_blend")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 base = texture(iChannel0, uv);\n"
            "    vec4 overlay = texture(iChannel1, uv);\n"
            "    float lumOver = dot(overlay.rgb, vec3(0.299, 0.587, 0.114));\n"
            "    float threshold = (iCustomParam1 != 0.0) ? iCustomParam1 : 0.5;\n"
            "    float alpha = smoothstep(threshold - 0.15, threshold + 0.15, lumOver);\n"
            "    vec3 blended = mix(base.rgb, overlay.rgb, alpha);\n"
            "    fragColor = vec4(blended, 1.0);\n"
            "}\n"
        );
    }

    // ── 4. High-Performance Color Grading & LUTs ─────────────────────────────
    if (p == QStringLiteral("cinematic_teal_orange") || p == QStringLiteral("teal_orange") || p == QStringLiteral("blockbuster")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 col = texture(iChannel0, uv);\n"
            "    float lum = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
            "    float strength = (iCustomParam1 != 0.0) ? iCustomParam1 : 0.65;\n"
            "    vec3 shadows = vec3(0.05, 0.35, 0.55);\n"
            "    vec3 highlights = vec3(1.0, 0.65, 0.25);\n"
            "    vec3 grade = mix(shadows, highlights, smoothstep(0.15, 0.85, lum));\n"
            "    vec3 result = mix(col.rgb, grade * col.rgb * 1.5, strength);\n"
            "    result = smoothstep(0.0, 1.0, result);\n"
            "    fragColor = vec4(result, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("filmic_aces_tonemap") || p == QStringLiteral("aces") || p == QStringLiteral("filmic")) {
        return QStringLiteral(
            "vec3 aces_fitted(vec3 x) {\n"
            "    float a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;\n"
            "    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);\n"
            "}\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 col = texture(iChannel0, uv);\n"
            "    float exposure = (iCustomParam1 != 0.0) ? iCustomParam1 : 1.2;\n"
            "    vec3 tonemapped = aces_fitted(col.rgb * exposure);\n"
            "    fragColor = vec4(tonemapped, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("chroma_keyer") || p == QStringLiteral("green_screen") || p == QStringLiteral("chromakey")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec4 col = texture(iChannel0, uv);\n"
            "    vec3 keyColor = vec3(0.05, 0.85, 0.15);\n"
            "    float similarity = (iCustomParam1 != 0.0) ? iCustomParam1 : 0.42;\n"
            "    float smoothness = (iCustomParam2 != 0.0) ? iCustomParam2 : 0.12;\n"
            "    float d = distance(col.rgb, keyColor);\n"
            "    float alpha = smoothstep(similarity, similarity + smoothness, d);\n"
            "    float maxRB = max(col.r, col.b);\n"
            "    vec3 despill = col.rgb;\n"
            "    if (despill.g > maxRB) despill.g = maxRB;\n"
            "    fragColor = vec4(mix(despill, col.rgb, alpha), alpha);\n"
            "}\n"
        );
    }

    // ── 5. Kernel Convolution Filtering ──────────────────────────────────────
    if (p == QStringLiteral("sobel_edge_sketch") || p == QStringLiteral("sobel") || p == QStringLiteral("sketch")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec2 px = 1.0 / iResolution.xy;\n"
            "    float mult = (iCustomParam1 != 0.0) ? iCustomParam1 : 1.5;\n"
            "    float tl = dot(texture(iChannel0, uv + vec2(-px.x,  px.y)).rgb, vec3(0.299, 0.587, 0.114));\n"
            "    float t  = dot(texture(iChannel0, uv + vec2( 0.0,   px.y)).rgb, vec3(0.299, 0.587, 0.114));\n"
            "    float tr = dot(texture(iChannel0, uv + vec2( px.x,  px.y)).rgb, vec3(0.299, 0.587, 0.114));\n"
            "    float l  = dot(texture(iChannel0, uv + vec2(-px.x,  0.0)).rgb,  vec3(0.299, 0.587, 0.114));\n"
            "    float r  = dot(texture(iChannel0, uv + vec2( px.x,  0.0)).rgb,  vec3(0.299, 0.587, 0.114));\n"
            "    float bl = dot(texture(iChannel0, uv + vec2(-px.x, -px.y)).rgb, vec3(0.299, 0.587, 0.114));\n"
            "    float b  = dot(texture(iChannel0, uv + vec2( 0.0,  -px.y)).rgb, vec3(0.299, 0.587, 0.114));\n"
            "    float br = dot(texture(iChannel0, uv + vec2( px.x, -px.y)).rgb, vec3(0.299, 0.587, 0.114));\n"
            "    float gx = -tl - 2.0*l - bl + tr + 2.0*r + br;\n"
            "    float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;\n"
            "    float edge = sqrt(gx*gx + gy*gy) * mult;\n"
            "    fragColor = vec4(vec3(edge), 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("laplacian_edge") || p == QStringLiteral("laplacian")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec2 px = 1.0 / iResolution.xy;\n"
            "    float c  = dot(texture(iChannel0, uv).rgb, vec3(0.333));\n"
            "    float t  = dot(texture(iChannel0, uv + vec2(0.0, px.y)).rgb, vec3(0.333));\n"
            "    float b  = dot(texture(iChannel0, uv - vec2(0.0, px.y)).rgb, vec3(0.333));\n"
            "    float l  = dot(texture(iChannel0, uv - vec2(px.x, 0.0)).rgb, vec3(0.333));\n"
            "    float r  = dot(texture(iChannel0, uv + vec2(px.x, 0.0)).rgb, vec3(0.333));\n"
            "    float lap = abs(4.0 * c - (t + b + l + r)) * ((iCustomParam1 != 0.0) ? iCustomParam1 : 2.5);\n"
            "    fragColor = vec4(vec3(lap), 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("sharpen_unsharp_mask") || p == QStringLiteral("sharpen") || p == QStringLiteral("unsharp_mask")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec2 px = 1.0 / iResolution.xy;\n"
            "    float amount = (iCustomParam1 != 0.0) ? iCustomParam1 : 1.5;\n"
            "    vec4 center = texture(iChannel0, uv);\n"
            "    vec4 top    = texture(iChannel0, uv + vec2(0.0, px.y));\n"
            "    vec4 bottom = texture(iChannel0, uv - vec2(0.0, px.y));\n"
            "    vec4 left   = texture(iChannel0, uv - vec2(px.x, 0.0));\n"
            "    vec4 right  = texture(iChannel0, uv + vec2(px.x, 0.0));\n"
            "    vec4 blurred = (top + bottom + left + right) * 0.25;\n"
            "    vec4 highPass = center - blurred;\n"
            "    fragColor = clamp(center + highPass * amount, 0.0, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("emboss") || p == QStringLiteral("relief")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    vec2 px = 1.0 / iResolution.xy;\n"
            "    vec3 c1 = texture(iChannel0, uv - px).rgb;\n"
            "    vec3 c2 = texture(iChannel0, uv + px).rgb;\n"
            "    vec3 diff = c2 - c1;\n"
            "    float val = dot(diff, vec3(0.333)) * ((iCustomParam1 != 0.0) ? iCustomParam1 : 2.0) + 0.5;\n"
            "    fragColor = vec4(vec3(val), 1.0);\n"
            "}\n"
        );
    }

    // ── Procedural Animations ────────────────────────────────────────────────
    if (p == QStringLiteral("neon_grid")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
            "    float speed = iTime * 0.8;\n"
            "    float z = 1.0 / (abs(uv.y) + 0.05);\n"
            "    vec2 grid = vec2(uv.x * z, z + speed);\n"
            "    vec2 lines = abs(fract(grid - 0.5) - 0.5) / fwidth(grid);\n"
            "    float lineVal = min(lines.x, lines.y);\n"
            "    float glow = 1.0 - min(lineVal, 1.0);\n"
            "    vec3 col = vec3(0.1, 0.7, 1.0) * glow * exp(-abs(uv.y) * 2.0);\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("plasma_energy")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    float v = sin(uv.x * 10.0 + iTime);\n"
            "    v += sin(uv.y * 10.0 + iTime * 0.5);\n"
            "    v += sin((uv.x + uv.y) * 10.0 + iTime);\n"
            "    vec2 c = uv + vec2(sin(iTime * 0.3), cos(iTime * 0.2));\n"
            "    v += sin(sqrt(c.x * c.x + c.y * c.y + 1.0) * 8.0 + iTime);\n"
            "    v = v * 0.5;\n"
            "    vec3 col = vec3(sin(v * 3.1415), cos(v * 3.1415 * 0.5), sin(v * 3.1415 + 2.0));\n"
            "    fragColor = vec4(col * 0.5 + 0.5, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("starfield_warp")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
            "    vec3 col = vec3(0.0);\n"
            "    float speed = iTime * 1.5;\n"
            "    for (float i = 0.0; i < 1.0; i += 0.2) {\n"
            "        float depth = fract(i + speed * 0.2);\n"
            "        float scale = mix(20.0, 0.5, depth);\n"
            "        float fade = depth * smoothstep(1.0, 0.9, depth);\n"
            "        vec2 p = uv * scale;\n"
            "        vec2 id = floor(p);\n"
            "        vec2 gv = fract(p) - 0.5;\n"
            "        float hash = fract(sin(dot(id + i * 100.0, vec2(12.9898, 78.233))) * 43758.5453);\n"
            "        if (hash > 0.85) {\n"
            "            float d = length(gv);\n"
            "            float star = smoothstep(0.15, 0.0, d) * fade;\n"
            "            col += vec3(star * (0.8 + 0.2 * hash), star * 0.9, star * 1.2);\n"
            "        }\n"
            "    }\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("cyber_matrix")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    float cols = 40.0;\n"
            "    float colId = floor(uv.x * cols);\n"
            "    float speed = 2.0 + fract(sin(colId * 133.3) * 4321.0) * 3.0;\n"
            "    float y = fract(uv.y + iTime * speed * 0.2 + fract(sin(colId * 77.7) * 9876.0));\n"
            "    float lead = smoothstep(0.98, 1.0, y);\n"
            "    float trail = (1.0 - y) * smoothstep(0.0, 0.2, y);\n"
            "    vec3 col = vec3(lead) * vec3(0.8, 1.0, 0.8) + vec3(trail) * vec3(0.0, 0.9, 0.2);\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n"
        );
    }
    if (p == QStringLiteral("gradient_flow")) {
        return QStringLiteral(
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    float t = iTime * 0.5;\n"
            "    vec3 col1 = vec3(0.95, 0.26, 0.45);\n"
            "    vec3 col2 = vec3(0.24, 0.44, 0.94);\n"
            "    vec3 col3 = vec3(0.98, 0.75, 0.18);\n"
            "    vec3 col4 = vec3(0.55, 0.20, 0.85);\n"
            "    float f1 = sin(uv.x * 3.14 + t) * 0.5 + 0.5;\n"
            "    float f2 = cos(uv.y * 3.14 - t * 0.7) * 0.5 + 0.5;\n"
            "    vec3 mixed1 = mix(col1, col2, f1);\n"
            "    vec3 mixed2 = mix(col3, col4, f2);\n"
            "    vec3 finalCol = mix(mixed1, mixed2, (uv.x + uv.y) * 0.5);\n"
            "    fragColor = vec4(finalCol, 1.0);\n"
            "}\n"
        );
    }

    return QString();
}
