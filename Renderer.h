/*
 * Renderer.h - rendering abstraction
 */
#pragma once

class Editor;

class Renderer {
public:
	virtual ~Renderer() = default;

	virtual void Draw(Editor &ed) = 0;
};