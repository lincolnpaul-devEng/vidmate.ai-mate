/*
    SPDX-FileCopyrightText: 2026 VidMate AI Team
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "glslshaderrenderer.h"
#include "shadervalidationengine.h"
#include <QDebug>
#include <QVector2D>
#include <QVector3D>
#include <QProcess>
#include <QFile>
#include <cmath>

struct VertexData {
    QVector2D position;
    QVector2D texCoord;
};

GLSLShaderRenderer::GLSLShaderRenderer(QObject *parent)
    : QObject(parent)
    , m_vbo(QOpenGLBuffer::VertexBuffer)
{
}

GLSLShaderRenderer::~GLSLShaderRenderer()
{
    bool madeCurrent = ShaderValidationEngine::instance()->makeCurrent();
    m_program.reset();
    m_fbo.reset();
    if (m_vao.isCreated()) {
        m_vao.destroy();
    }
    if (m_vbo.isCreated()) {
        m_vbo.destroy();
    }
    if (madeCurrent) {
        ShaderValidationEngine::instance()->doneCurrent();
    }
}

void GLSLShaderRenderer::initQuadGeometry()
{
    if (m_glInitialized) {
        return;
    }

    initializeOpenGLFunctions();

    if (!m_vao.isCreated()) {
        m_vao.create();
    }
    m_vao.bind();

    // Standard fullscreen quad (2 triangles / 4 vertices strip)
    static const VertexData quadVertices[] = {
        {QVector2D(-1.0f, -1.0f), QVector2D(0.0f, 0.0f)},
        {QVector2D( 1.0f, -1.0f), QVector2D(1.0f, 0.0f)},
        {QVector2D(-1.0f,  1.0f), QVector2D(0.0f, 1.0f)},
        {QVector2D( 1.0f,  1.0f), QVector2D(1.0f, 1.0f)}
    };

    if (!m_vbo.isCreated()) {
        m_vbo.create();
    }
    m_vbo.bind();
    m_vbo.allocate(quadVertices, sizeof(quadVertices));

    m_vao.release();
    m_vbo.release();
    m_glInitialized = true;
}

bool GLSLShaderRenderer::loadShader(const QString &fullGlslSource, QString *outError)
{
    if (!ShaderValidationEngine::instance()->makeCurrent()) {
        if (outError) *outError = QStringLiteral("Failed to activate OpenGL context for shader loading.");
        return false;
    }

    if (!m_glInitialized) {
        initQuadGeometry();
    }

    auto newProgram = std::make_unique<QOpenGLShaderProgram>();

    if (!newProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, ShaderValidationEngine::standardVertexShader())) {
        if (outError) *outError = newProgram->log();
        ShaderValidationEngine::instance()->doneCurrent();
        return false;
    }

    if (!newProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fullGlslSource)) {
        if (outError) *outError = newProgram->log();
        ShaderValidationEngine::instance()->doneCurrent();
        return false;
    }

    if (!newProgram->link()) {
        if (outError) *outError = newProgram->log();
        ShaderValidationEngine::instance()->doneCurrent();
        return false;
    }

    m_program = std::move(newProgram);
    ShaderValidationEngine::instance()->doneCurrent();
    return true;
}

GLuint GLSLShaderRenderer::renderToTexture(int width, int height, const UniformState &uniforms)
{
    if (!isValid() || width <= 0 || height <= 0) {
        return 0;
    }

    if (!ShaderValidationEngine::instance()->makeCurrent()) {
        return 0;
    }

    if (!m_glInitialized) {
        initQuadGeometry();
    }

    // Recreate FBO if size changed
    if (!m_fbo || m_fbo->width() != width || m_fbo->height() != height) {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setTextureTarget(GL_TEXTURE_2D);
        format.setInternalTextureFormat(GL_RGBA8);
        m_fbo = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
    }

    m_fbo->bind();
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_program->bind();

    // 1. Pass global video timeline uniforms
    m_program->setUniformValue("iResolution", QVector3D(float(width), float(height), 1.0f));
    m_program->setUniformValue("iTime", uniforms.time);
    m_program->setUniformValue("iTimeDelta", uniforms.timeDelta);
    m_program->setUniformValue("iFrame", uniforms.frame);
    m_program->setUniformValue("iMouse", uniforms.mouse);
    m_program->setUniformValue("iAudioLevels", uniforms.audioLevels);

    // 2. Pass colors
    m_program->setUniformValue("iColor1", QVector4D(uniforms.color1.redF(), uniforms.color1.greenF(), uniforms.color1.blueF(), uniforms.color1.alphaF()));
    m_program->setUniformValue("iColor2", QVector4D(uniforms.color2.redF(), uniforms.color2.greenF(), uniforms.color2.blueF(), uniforms.color2.alphaF()));

    // 3. Pass custom procedural / keyframe parameters (iCustomParam1..8)
    for (int i = 1; i <= 8; ++i) {
        const QString name = QStringLiteral("iCustomParam%1").arg(i);
        float val = uniforms.customParams.value(i, 0.0f);
        m_program->setUniformValue(name.toUtf8().constData(), val);
    }

    // 4. Bind input textures (iChannel0..3)
    for (int c = 0; c < 4; ++c) {
        if (uniforms.channelTextures[c] > 0) {
            glActiveTexture(GL_TEXTURE0 + c);
            glBindTexture(GL_TEXTURE_2D, uniforms.channelTextures[c]);
            const QString chanName = QStringLiteral("iChannel%1").arg(c);
            m_program->setUniformValue(chanName.toUtf8().constData(), c);
        }
    }

    // 5. Draw quad
    if (m_vao.isCreated()) {
        m_vao.bind();
    }
    m_vbo.bind();
    int posLoc = m_program->attributeLocation("in_position");
    int texLoc = m_program->attributeLocation("in_texCoord");

    if (posLoc != -1) {
        m_program->enableAttributeArray(posLoc);
        m_program->setAttributeBuffer(posLoc, GL_FLOAT, offsetof(VertexData, position), 2, sizeof(VertexData));
    }
    if (texLoc != -1) {
        m_program->enableAttributeArray(texLoc);
        m_program->setAttributeBuffer(texLoc, GL_FLOAT, offsetof(VertexData, texCoord), 2, sizeof(VertexData));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (posLoc != -1) m_program->disableAttributeArray(posLoc);
    if (texLoc != -1) m_program->disableAttributeArray(texLoc);

    m_vbo.release();
    if (m_vao.isCreated()) {
        m_vao.release();
    }
    m_program->release();
    m_fbo->release();

    GLuint tex = m_fbo->texture();
    ShaderValidationEngine::instance()->doneCurrent();
    return tex;
}

QImage GLSLShaderRenderer::renderToImage(int width, int height, const UniformState &uniforms)
{
    if (!isValid() || width <= 0 || height <= 0) {
        return QImage();
    }

    if (!ShaderValidationEngine::instance()->makeCurrent()) {
        return QImage();
    }

    if (!m_glInitialized) {
        initQuadGeometry();
    }

    // Recreate FBO if size changed
    if (!m_fbo || m_fbo->width() != width || m_fbo->height() != height) {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setTextureTarget(GL_TEXTURE_2D);
        format.setInternalTextureFormat(GL_RGBA8);
        m_fbo = std::make_unique<QOpenGLFramebufferObject>(width, height, format);
    }

    m_fbo->bind();
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_program->bind();

    // 1. Pass global video timeline uniforms
    m_program->setUniformValue("iResolution", QVector3D(float(width), float(height), 1.0f));
    m_program->setUniformValue("iTime", uniforms.time);
    m_program->setUniformValue("iTimeDelta", uniforms.timeDelta);
    m_program->setUniformValue("iFrameRate", uniforms.timeDelta > 0.0001f ? (1.0f / uniforms.timeDelta) : 30.0f);
    m_program->setUniformValue("iFrame", uniforms.frame);
    m_program->setUniformValue("iMouse", uniforms.mouse);
    m_program->setUniformValue("iAudioLevels", uniforms.audioLevels);

    // 2. Pass colors
    m_program->setUniformValue("iColor1", QVector4D(uniforms.color1.redF(), uniforms.color1.greenF(), uniforms.color1.blueF(), uniforms.color1.alphaF()));
    m_program->setUniformValue("iColor2", QVector4D(uniforms.color2.redF(), uniforms.color2.greenF(), uniforms.color2.blueF(), uniforms.color2.alphaF()));

    // 3. Pass custom procedural / keyframe parameters (iCustomParam1..8)
    for (int i = 1; i <= 8; ++i) {
        const QString name = QStringLiteral("iCustomParam%1").arg(i);
        float val = uniforms.customParams.value(i, 0.0f);
        m_program->setUniformValue(name.toUtf8().constData(), val);
    }

    // 4. Bind input textures (iChannel0..3)
    for (int c = 0; c < 4; ++c) {
        if (uniforms.channelTextures[c] > 0) {
            glActiveTexture(GL_TEXTURE0 + c);
            glBindTexture(GL_TEXTURE_2D, uniforms.channelTextures[c]);
            const QString chanName = QStringLiteral("iChannel%1").arg(c);
            m_program->setUniformValue(chanName.toUtf8().constData(), c);
        }
    }

    // 5. Draw quad
    if (m_vao.isCreated()) {
        m_vao.bind();
    }
    m_vbo.bind();
    int posLoc = m_program->attributeLocation("in_position");
    int texLoc = m_program->attributeLocation("in_texCoord");

    if (posLoc != -1) {
        m_program->enableAttributeArray(posLoc);
        m_program->setAttributeBuffer(posLoc, GL_FLOAT, offsetof(VertexData, position), 2, sizeof(VertexData));
    }
    if (texLoc != -1) {
        m_program->enableAttributeArray(texLoc);
        m_program->setAttributeBuffer(texLoc, GL_FLOAT, offsetof(VertexData, texCoord), 2, sizeof(VertexData));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (posLoc != -1) m_program->disableAttributeArray(posLoc);
    if (texLoc != -1) m_program->disableAttributeArray(texLoc);

    m_vbo.release();
    if (m_vao.isCreated()) {
        m_vao.release();
    }
    m_program->release();
    m_fbo->release();

    QImage img = m_fbo->toImage();
    ShaderValidationEngine::instance()->doneCurrent();
    return img;
}

bool GLSLShaderRenderer::renderToVideoFile(const QString &outputPath, int width, int height, double fps, int totalFrames, bool audioReactive, QString *outError)
{
    if (!isValid()) {
        if (outError) *outError = QStringLiteral("Shader is not compiled or linked.");
        return false;
    }

    if (width <= 0) width = 1920;
    if (height <= 0) height = 1080;
    if (fps <= 0.0) fps = 30.0;
    if (totalFrames <= 0) totalFrames = int(fps * 10.0);

    QProcess ffmpeg;
    QStringList args = {
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-vcodec"), QStringLiteral("rawvideo"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(width).arg(height),
        QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
        QStringLiteral("-r"), QString::number(fps, 'f', 2),
        QStringLiteral("-i"), QStringLiteral("-"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-preset"), QStringLiteral("veryfast"),
        QStringLiteral("-crf"), QStringLiteral("18"),
        outputPath
    };

    ffmpeg.start(QStringLiteral("ffmpeg"), args);
    if (!ffmpeg.waitForStarted(5000)) {
        if (outError) *outError = QStringLiteral("Failed to launch ffmpeg for procedural shader video rendering.");
        return false;
    }

    UniformState uState;
    uState.timeDelta = float(1.0 / fps);

    for (int f = 0; f < totalFrames; ++f) {
        float t = float(f) / float(fps);
        uState.time = t;
        uState.frame = f;
        if (audioReactive) {
            uState.audioLevels = 0.45f + 0.35f * std::sin(t * 8.0f) + 0.20f * std::sin(t * 19.5f);
        } else {
            uState.audioLevels = 0.5f;
        }

        QImage img = renderToImage(width, height, uState);
        if (img.isNull()) {
            if (outError) *outError = QStringLiteral("Failed to render procedural shader frame %1.").arg(f);
            ffmpeg.kill();
            return false;
        }

        if (img.format() != QImage::Format_RGBA8888) {
            img = img.convertToFormat(QImage::Format_RGBA8888);
        }

        qint64 bytesWritten = ffmpeg.write(reinterpret_cast<const char*>(img.constBits()), img.sizeInBytes());
        if (bytesWritten < 0) {
            if (outError) *outError = QStringLiteral("Error piping raw frames to ffmpeg: %1").arg(ffmpeg.errorString());
            ffmpeg.kill();
            return false;
        }

        if (f % 30 == 0) {
            ffmpeg.waitForBytesWritten(200);
        }
    }

    ffmpeg.closeWriteChannel();
    if (!ffmpeg.waitForFinished(45000)) {
        ffmpeg.kill();
        if (outError) *outError = QStringLiteral("ffmpeg encoding timed out.");
        return false;
    }

    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        if (outError) *outError = QString::fromUtf8(ffmpeg.readAllStandardError());
        return false;
    }

    return QFile::exists(outputPath);
}

bool GLSLShaderRenderer::processVideoWithShader(const QString &inputVideoPath,
                                                const QString &outputPath,
                                                int width,
                                                int height,
                                                double fps,
                                                const QString &secondInputVideoPath,
                                                bool enableTemporalBuffer,
                                                const UniformState &baseUniforms,
                                                QString *outError)
{
    if (!isValid()) {
        if (outError) *outError = QStringLiteral("Shader is not compiled or linked.");
        return false;
    }
    if (!QFile::exists(inputVideoPath)) {
        if (outError) *outError = QStringLiteral("Input video file does not exist: %1").arg(inputVideoPath);
        return false;
    }

    // Auto-detect resolution and frame rate if not specified
    if (width <= 0 || height <= 0 || fps <= 0.0) {
        QProcess probe;
        probe.start(QStringLiteral("ffprobe"), {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-select_streams"), QStringLiteral("v:0"),
            QStringLiteral("-show_entries"), QStringLiteral("stream=width,height,r_frame_rate"),
            QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
            inputVideoPath
        });
        if (probe.waitForFinished(3000)) {
            QStringList lines = QString::fromUtf8(probe.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (lines.size() >= 3) {
                if (width <= 0) width = lines[0].trimmed().toInt();
                if (height <= 0) height = lines[1].trimmed().toInt();
                if (fps <= 0.0) {
                    QString rFps = lines[2].trimmed();
                    if (rFps.contains(QLatin1Char('/'))) {
                        auto parts = rFps.split(QLatin1Char('/'));
                        if (parts.size() == 2 && parts[1].toDouble() > 0) {
                            fps = parts[0].toDouble() / parts[1].toDouble();
                        }
                    } else {
                        fps = rFps.toDouble();
                    }
                }
            }
        }
    }

    if (width <= 0) width = 1920;
    if (height <= 0) height = 1080;
    if (fps <= 0.0) fps = 30.0;

    // Launch decoder process for source video
    QProcess decFfmpeg;
    decFfmpeg.start(QStringLiteral("ffmpeg"), {
        QStringLiteral("-i"), inputVideoPath,
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(width).arg(height),
        QStringLiteral("-")
    });
    if (!decFfmpeg.waitForStarted(5000)) {
        if (outError) *outError = QStringLiteral("Failed to launch ffmpeg decoder for: %1").arg(inputVideoPath);
        return false;
    }

    // Optional second video decoder for multi-texture / displacement
    QProcess dec2Ffmpeg;
    bool hasSecond = !secondInputVideoPath.isEmpty() && QFile::exists(secondInputVideoPath);
    if (hasSecond) {
        dec2Ffmpeg.start(QStringLiteral("ffmpeg"), {
            QStringLiteral("-i"), secondInputVideoPath,
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
            QStringLiteral("-s"), QStringLiteral("%1x%2").arg(width).arg(height),
            QStringLiteral("-")
        });
        dec2Ffmpeg.waitForStarted(5000);
    }

    // Launch encoder process for output video (copy original audio if present)
    QProcess encFfmpeg;
    QStringList encArgs = {
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-vcodec"), QStringLiteral("rawvideo"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(width).arg(height),
        QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
        QStringLiteral("-r"), QString::number(fps, 'f', 2),
        QStringLiteral("-i"), QStringLiteral("-"),
        QStringLiteral("-i"), inputVideoPath,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-map"), QStringLiteral("1:a:0?"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-preset"), QStringLiteral("veryfast"),
        QStringLiteral("-crf"), QStringLiteral("18"),
        QStringLiteral("-c:a"), QStringLiteral("copy"),
        outputPath
    };
    encFfmpeg.start(QStringLiteral("ffmpeg"), encArgs);
    if (!encFfmpeg.waitForStarted(5000)) {
        decFfmpeg.kill();
        if (hasSecond) dec2Ffmpeg.kill();
        if (outError) *outError = QStringLiteral("Failed to launch ffmpeg encoder for output: %1").arg(outputPath);
        return false;
    }

    if (!ShaderValidationEngine::instance()->makeCurrent()) {
        decFfmpeg.kill();
        if (hasSecond) dec2Ffmpeg.kill();
        encFfmpeg.kill();
        if (outError) *outError = QStringLiteral("Failed to activate OpenGL context.");
        return false;
    }

    GLuint inputTex0 = 0;
    GLuint inputTex1 = 0;
    GLuint prevTex = 0;

    glGenTextures(1, &inputTex0);
    glBindTexture(GL_TEXTURE_2D, inputTex0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (hasSecond) {
        glGenTextures(1, &inputTex1);
        glBindTexture(GL_TEXTURE_2D, inputTex1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    if (enableTemporalBuffer) {
        glGenTextures(1, &prevTex);
        glBindTexture(GL_TEXTURE_2D, prevTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    ShaderValidationEngine::instance()->doneCurrent();

    const qint64 frameBytes = qint64(width) * height * 4;
    QByteArray frameBuf;
    QByteArray frame2Buf;
    int frameIndex = 0;

    UniformState uState = baseUniforms;
    uState.timeDelta = float(1.0 / fps);
    uState.channelTextures[0] = inputTex0;
    uState.channelTextures[1] = inputTex1;
    uState.channelTextures[2] = prevTex;

    while (decFfmpeg.state() == QProcess::Running || decFfmpeg.bytesAvailable() > 0) {
        while (frameBuf.size() < frameBytes && (decFfmpeg.bytesAvailable() > 0 || decFfmpeg.waitForReadyRead(500))) {
            frameBuf.append(decFfmpeg.read(frameBytes - frameBuf.size()));
        }
        if (frameBuf.size() < frameBytes) break;

        if (ShaderValidationEngine::instance()->makeCurrent()) {
            glBindTexture(GL_TEXTURE_2D, inputTex0);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frameBuf.constData());
            frameBuf.remove(0, frameBytes);

            if (hasSecond && dec2Ffmpeg.state() == QProcess::Running) {
                while (frame2Buf.size() < frameBytes && (dec2Ffmpeg.bytesAvailable() > 0 || dec2Ffmpeg.waitForReadyRead(100))) {
                    frame2Buf.append(dec2Ffmpeg.read(frameBytes - frame2Buf.size()));
                }
                if (frame2Buf.size() >= frameBytes) {
                    glBindTexture(GL_TEXTURE_2D, inputTex1);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frame2Buf.constData());
                    frame2Buf.remove(0, frameBytes);
                }
            }
            ShaderValidationEngine::instance()->doneCurrent();
        }

        float t = float(frameIndex) / float(fps);
        uState.time = t;
        uState.frame = frameIndex;

        QImage outImg = renderToImage(width, height, uState);
        if (!outImg.isNull()) {
            if (outImg.format() != QImage::Format_RGBA8888) {
                outImg = outImg.convertToFormat(QImage::Format_RGBA8888);
            }
            encFfmpeg.write(reinterpret_cast<const char*>(outImg.constBits()), outImg.sizeInBytes());

            if (enableTemporalBuffer && prevTex > 0 && ShaderValidationEngine::instance()->makeCurrent()) {
                glBindTexture(GL_TEXTURE_2D, prevTex);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outImg.constBits());
                ShaderValidationEngine::instance()->doneCurrent();
            }
        }

        frameIndex++;
        if (frameIndex % 30 == 0) {
            encFfmpeg.waitForBytesWritten(200);
        }
    }

    encFfmpeg.closeWriteChannel();
    decFfmpeg.waitForFinished(10000);
    if (hasSecond) dec2Ffmpeg.waitForFinished(10000);

    if (!encFfmpeg.waitForFinished(60000)) {
        encFfmpeg.kill();
        if (outError) *outError = QStringLiteral("Encoding timed out for %1").arg(outputPath);
    }

    if (ShaderValidationEngine::instance()->makeCurrent()) {
        if (inputTex0) glDeleteTextures(1, &inputTex0);
        if (inputTex1) glDeleteTextures(1, &inputTex1);
        if (prevTex) glDeleteTextures(1, &prevTex);
        ShaderValidationEngine::instance()->doneCurrent();
    }

    return QFile::exists(outputPath);
}

bool GLSLShaderRenderer::processVideoWithShader(const QString &inputVideoPath,
                                                const QString &outputPath,
                                                int width,
                                                int height,
                                                double fps,
                                                const QString &secondInputVideoPath,
                                                bool enableTemporalBuffer,
                                                QString *outError)
{
    UniformState defaultState;
    return processVideoWithShader(inputVideoPath, outputPath, width, height, fps, secondInputVideoPath, enableTemporalBuffer, defaultState, outError);
}
