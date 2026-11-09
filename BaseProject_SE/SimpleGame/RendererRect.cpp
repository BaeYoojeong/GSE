#include "stdafx.h"
#include "Renderer.h"

// Non-uniform rectangle drawing used by the first environment prototype.
void Renderer::DrawSolidRect(float x, float y, float z, float width, float height, float r, float g, float b,
                             float a)
{
    float newX, newY;
    GetGLPosition(x, y, &newX, &newY);

    glUseProgram(m_SolidRectShader);
    glUniform4f(glGetUniformLocation(m_SolidRectShader, "u_Trans"), newX, newY, width, height);
    glUniform4f(glGetUniformLocation(m_SolidRectShader, "u_Color"), r, g, b, a);

    const GLint attribPosition = glGetAttribLocation(m_SolidRectShader, "a_Position");
    glEnableVertexAttribArray(attribPosition);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBORect);
    glVertexAttribPointer(attribPosition, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(attribPosition);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
