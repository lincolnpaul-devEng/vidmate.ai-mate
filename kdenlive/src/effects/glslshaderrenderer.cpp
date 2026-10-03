/*
    SPDX-FileCopyrightText: 2026 VidMate AI Team
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "glslshaderrenderer.h"
#include "shadervalidationengine.h"
#include <QDebug>
#include <QVector2D>
#include <QVector3D>

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
    if (m_vbo.isCreated()) {
        m_vbo.destroy();
    }
}

void GLSLShaderRenderer::initQuadGeometry()
{
    if (m_vbo.isCreated()) {
        return;
    }

    initializeOpenGLFunctions();

    // Standard fullscreen quad (2 triangles / 4 vertices strip)
    static const VertexData quadVertices[] = {
        {QVector2D(-1.0f, -1.0f), QVector2D(0.0f, 0.0f)},
        {QVector2D( 1.0f, -1.0f), QVector2D(1.0f, 0.0f)},
        {QVector2D(-1.0f,  1.0f), QVector2D(0.0f, 1.0f)},
        {QVector2D( 1.0f,  1.0f), QVector2D(1.0f, 1.0f)}
    };

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(quadVertices, sizeof(quadVertices));
    m_vbo.release();
    m_glInitialized = true;
}

bool GLSLShaderRenderer::loadShader(const QString &fullGlslSource, QString *outError)
{
    if (!m_glInitialized) {
        initQuadGeometry();
    }

    auto newProgram = std::make_unique<QOpenGLShaderProgram>();

    if (!newProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, ShaderValidationEngine::standardVertexShader())) {
        if (outError) *outError = newProgram->log();
        return false;
    }

    if (!newProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fullGlslSource)) {
        if (outError) *outError = newProgram->log();
        return false;
    }

    if (!newProgram->link()) {
        if (outError) *outError = newProgram->log();
        return false;
    }

    m_program = std::move(newProgram);
    return true;
}

GLuint GLSLShaderRenderer::renderToTexture(int width, int height, const UniformState &uniforms)
{
    if (!isValid() || width <= 0 || height <= 0) {
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
    m_program->release();
    m_fbo->release();

    return m_fbo->texture();
}

QImage GLSLShaderRenderer::renderToImage(int width, int height, const UniformState &uniforms)
{
    renderToTexture(width, height, uniforms);
    if (m_fbo) {
        return m_fbo->toImage();
    }
    return QImage();
}
