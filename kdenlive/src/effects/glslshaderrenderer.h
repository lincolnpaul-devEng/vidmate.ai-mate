/*
    SPDX-FileCopyrightText: 2026 VidMate AI Team
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QObject>
#include <QColor>
#include <QMap>
#include <QVector4D>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <memory>

/**
 * @class GLSLShaderRenderer
 * @brief High-performance GPU procedural shader renderer capable of evaluating
 *        ShaderToy / SDF code at 60+ FPS with keyframed uniform parameters.
 */
class GLSLShaderRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    struct UniformState {
        float time{0.0f};
        float timeDelta{0.0166f};
        int frame{0};
        QVector4D mouse{0.0f, 0.0f, 0.0f, 0.0f};
        float audioLevels{0.0f};
        QMap<int, float> customParams; // 1 -> iCustomParam1, 2 -> iCustomParam2...
        QColor color1{Qt::white};
        QColor color2{Qt::black};
        GLuint channelTextures[4]{0, 0, 0, 0};
    };

    explicit GLSLShaderRenderer(QObject *parent = nullptr);
    ~GLSLShaderRenderer() override;

    /**
     * @brief Loads and compiles a validated GLSL fragment shader source.
     */
    bool loadShader(const QString &fullGlslSource, QString *outError = nullptr);

    /**
     * @brief Renders the procedural shader to an internal or target FBO.
     * @param width Output resolution width (pixels).
     * @param height Output resolution height (pixels).
     * @param uniforms Current timeline keyframe values, time, audio levels.
     * @return Texture ID of the rendered result.
     */
    GLuint renderToTexture(int width, int height, const UniformState &uniforms);

    /**
     * @brief Renders the shader and extracts a QImage (useful for thumbnails, exports, offline buffers).
     */
    QImage renderToImage(int width, int height, const UniformState &uniforms);

    /**
     * @brief Renders the procedural animated shader across timeline frames to an MP4 video file.
     * @param outputPath Output .mp4 video file path.
     * @param width Video width in pixels.
     * @param height Video height in pixels.
     * @param fps Frame rate (e.g. 30.0 or 60.0).
     * @param totalFrames Number of frames to animate and render.
     * @param audioReactive If true, modulates audioLevels uniform with rhythmic energy curves.
     * @param outError Optional error string output.
     * @return True if video rendered and encoded successfully.
     */
    bool renderToVideoFile(const QString &outputPath, int width, int height, double fps, int totalFrames, bool audioReactive = false, QString *outError = nullptr);

    bool isValid() const { return m_program && m_program->isLinked(); }

private:
    void initQuadGeometry();

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    bool m_glInitialized{false};
};
