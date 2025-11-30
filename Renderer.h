/*
 * Renderer.h - rendering abstraction
 */
#ifndef KTE_RENDERER_H
#define KTE_RENDERER_H

class Editor;

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void Draw(const Editor &ed) = 0;
};

#endif // KTE_RENDERER_H
