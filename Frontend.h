/*
 * Frontend.h - top-level container that couples Input + Renderer and runs the loop
 */
#ifndef KTE_FRONTEND_H
#define KTE_FRONTEND_H


class Editor;
class InputHandler;
class Renderer;

class Frontend {
public:
	virtual ~Frontend() = default;

	// Initialize the frontend (create window/terminal, etc.)
	virtual bool Init(Editor &ed) = 0;

	// Execute one iteration (poll input, dispatch, draw). Set running=false to exit.
	virtual void Step(Editor &ed, bool &running) = 0;

	// Shutdown/cleanup
	virtual void Shutdown() = 0;
};

#endif // KTE_FRONTEND_H
