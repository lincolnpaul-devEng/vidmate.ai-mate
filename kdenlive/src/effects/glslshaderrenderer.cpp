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
